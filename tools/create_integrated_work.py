#!/usr/bin/env python3
"""Create a separate, checked image work set without building or importing a ZIP.

The source system-app-staging images are never modified. Additional system
files must be explicitly listed in a JSON manifest with pinned content hashes;
this prevents accidentally incorporating half-finished first-boot files.
"""
import argparse
import hashlib
import json
from pathlib import Path
import re
import shutil
import subprocess

from patch_images import Editor, PatchError, ROOT, SYSTEM_LABEL, digest, safe_target


SOURCE = ROOT / 'build/system-app-staging'
SOURCE_SHA = {
    'boot.img': 'eeafdc55821ad9416e1da374dfadf7ae39a478fd3ad5ec6b5fb2d800e4a36b6e',
    'system.img': '70b3bd94657582588cba9d4f2f3a228ff1c3087988d169d4841dd91a405fae66',
    'vendor.img': 'fd1cb5abfb6b5574d6558a4c021aa86dbe32c0e93ace42281c5d76e1d626684b',
}
MDM = {
    'target': '/system/framework/codex-mdm-compat.jar',
    'source': 'build/mdm-compat-candidate.jar',
    'sha256': 'a183f0c5c55ef931bc9a84fec60f1a21bb5164bf183a6ba2ea90049f5df1ab3d',
    'old_sha256': 'fe708389b0333d4629c945691c8250590c310fa242065ac17020121a9ee07bff',
    'mode': '0644',
}
HEX = re.compile(r'^[a-f0-9]{64}$')


def entries_from_manifest(path):
    entries = [dict(MDM)]
    if path is not None:
        data = json.loads(path.read_text(encoding='utf-8'))
        if set(data) != {'files'} or not isinstance(data['files'], list):
            raise PatchError('manifest must be an object containing only files')
        entries.extend(data['files'])
    seen = set()
    for entry in entries:
        if not isinstance(entry, dict) or set(entry) != {'target', 'source', 'sha256', 'old_sha256', 'mode'}:
            raise PatchError('each file requires target, source, sha256, old_sha256, mode')
        target = safe_target(entry['target'])
        if not target.startswith('/system/') or target in seen:
            raise PatchError('duplicate or non-system target: ' + target)
        seen.add(target)
        path = (ROOT / entry['source']).resolve()
        if not path.is_relative_to(ROOT.resolve()) or not path.is_file():
            raise PatchError('source must be an existing workspace file: ' + str(path))
        if not HEX.fullmatch(entry['sha256']) or digest(path) != entry['sha256']:
            raise PatchError('source SHA mismatch: ' + str(path))
        if entry['old_sha256'] is not None and not HEX.fullmatch(entry['old_sha256']):
            raise PatchError('invalid old SHA for ' + target)
        if entry['mode'] not in ('0644', '0755', '0777'):
            raise PatchError('unsupported mode for ' + target)
        entry['target'], entry['source'] = target, path
    return entries


def preflight_files(editor, entries):
    for entry in entries:
        existing = editor.stat(entry['target'])
        old = entry['old_sha256']
        if old is None:
            if existing:
                raise PatchError('new target already exists: ' + entry['target'])
        else:
            editor.verify_file(entry['target'], old, int(entry['mode'], 8), SYSTEM_LABEL)


def apply_files(editor, entries):
    preflight_files(editor, entries)
    for entry in entries:
        editor.put(entry['target'], entry['source'], int(entry['mode'], 8), SYSTEM_LABEL,
                   replace=entry['old_sha256'] is not None)
    for entry in entries:
        editor.verify_file(entry['target'], entry['sha256'], int(entry['mode'], 8), SYSTEM_LABEL)


def copy_image(source, destination, expected):
    h = hashlib.sha256()
    with source.open('rb') as inp, destination.open('xb') as out:
        while True:
            block = inp.read(4 * 1024 * 1024)
            if not block:
                break
            out.write(block)
            h.update(block)
    if h.hexdigest() != expected:
        raise PatchError('source image checksum mismatch: ' + str(source))


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--output', type=Path, required=True, help='new work directory; must not exist')
    parser.add_argument('--manifest', type=Path, help='optional pinned system-file manifest')
    parser.add_argument('--apply', action='store_true', help='create and patch independent image copies')
    args = parser.parse_args()
    output = args.output.resolve()
    partial = output.with_name(output.name + '.partial')
    if (output.exists() or partial.exists() or output.is_relative_to(SOURCE.resolve())
            or SOURCE.resolve().is_relative_to(output)):
        raise PatchError('output or temporary output exists or aliases source')
    if not all(HEX.fullmatch(x) for x in SOURCE_SHA.values()):
        raise PatchError('source image hashes have not all been pinned')
    entries = entries_from_manifest(args.manifest)
    for image in SOURCE_SHA:
        if not (SOURCE / image).is_file():
            raise PatchError('missing source image: ' + image)
    preflight_files(Editor(SOURCE / 'system.img'), entries)
    print('Files preflighted:', *(e['target'] for e in entries), sep='\n  ', flush=True)
    if not args.apply:
        print('No writes; image hashes will be verified while copying on --apply.')
        return
    partial.mkdir(parents=True)
    try:
        for name, expected in SOURCE_SHA.items():
            print('Copying and hashing', name, flush=True)
            copy_image(SOURCE / name, partial / name, expected)
        apply_files(Editor(partial / 'system.img'), entries)
        result = {}
        for name in ('system.img', 'vendor.img'):
            p = subprocess.run(['e2fsck', '-fn', str(partial / name)], stdout=subprocess.PIPE,
                               stderr=subprocess.STDOUT, text=True)
            if p.returncode:
                raise PatchError('e2fsck failed for ' + name + ': ' + p.stdout[-2000:])
            result[name] = digest(partial / name)
            print(name + ' SHA-256=' + result[name], flush=True)
        result['boot.img'] = SOURCE_SHA['boot.img']
        (partial / 'integration.json').write_text(json.dumps({'images': result,
            'files': [{**e, 'source': str(e['source'].relative_to(ROOT.resolve()))} for e in entries]},
            indent=2) + '\n', encoding='utf-8')
        partial.rename(output)
    except BaseException:
        # The only directory removed here was created by this invocation and
        # did not exist at the start. Never touch source or existing output.
        shutil.rmtree(partial)
        raise
    print('Independent image work set ready:', output)
    print('No ZIP created or device imported.')


if __name__ == '__main__':
    try:
        main()
    except (OSError, PatchError, ValueError, json.JSONDecodeError) as exc:
        raise SystemExit('Error: ' + str(exc))
