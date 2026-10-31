#!/usr/bin/env python3
"""Create an independent single-SIM and sound-default ROM revision; no ZIP/device."""
import argparse
import hashlib
import json
from pathlib import Path
import re
import shutil
import subprocess
import tempfile

from patch_images import Editor, PatchError, ROOT, SYSTEM_LABEL, VENDOR_LABEL, digest
from create_integrated_work import copy_image

BASE_IMAGES = {
    'boot.img': 'eeafdc55821ad9416e1da374dfadf7ae39a478fd3ad5ec6b5fb2d800e4a36b6e',
    'system.img': 'd7b0df86f1189b1131dc12fd717d3c1e360aca03ecfc69e863e9726066d52257',
    'vendor.img': 'db36d9d1b3d07b2d8ed152ba1956e5d5c658bc6f44af879f02516595e853e616',
}
ORIGINAL_PROPS = {
    'system.img': '4cf12f792ad32dbc0aa042c1fb1a13fb96fa79768bf11a948b0b1f123109a414',
    'vendor.img': 'f87373c177ccb6f7cbbffb436820cb04d585a27f7b54b263ddc07d3e42a587af',
}
RADIO = {'persist.radio.multisim.config': ('dsds', '')}
SOUNDS = {
    'ro.config.ringtone': ('Ring_Synth_04.ogg', 'Huawei_Tune_Harmony.ogg'),
    'ro.config.notification_sound': ('OnTheHunt.ogg', 'Bongo.ogg'),
    'ro.config.alarm_alert': ('Alarm_Classic.ogg', 'Morning_Light.ogg'),
}
PARTS = {
    'system.img': ('/system/build.prop', SYSTEM_LABEL, RADIO),
    'vendor.img': ('/build.prop', VENDOR_LABEL, {**RADIO, **SOUNDS}),
}


def patch_properties(data, changes):
    lines = data.splitlines(keepends=True)
    for key, (old, new) in changes.items():
        prefix = key.encode('ascii') + b'='
        matches = [i for i, line in enumerate(lines) if line.startswith(prefix)]
        if len(matches) != 1:
            raise ValueError(f'{key}: expected exactly one active assignment, found {len(matches)}')
        i = matches[0]
        end = b'\r\n' if lines[i].endswith(b'\r\n') else b'\n' if lines[i].endswith(b'\n') else b''
        if lines[i] != prefix + old.encode('ascii') + end:
            raise ValueError(f'{key}: unexpected original value')
        lines[i] = prefix + new.encode('ascii') + end
    return b''.join(lines)


def file_snapshot(editor, path, label):
    stat = editor.stat(path)
    if not stat or not re.search(r'Type:\s*regular\s+Mode:\s*0600\b', stat):
        raise ValueError('unexpected type/mode: ' + path)
    if not re.search(r'User:\s*0\s+Group:\s*0\s+Project:\s*0', stat) or not re.search(r'Links:\s*1\b', stat):
        raise ValueError('unexpected owner/project/link count: ' + path)
    attrs = editor.command('ea_list ' + path)
    names = re.findall(r'^\s+(\S+) \(\d+\)\s*=', attrs, re.M)
    if names != ['security.selinux']:
        raise ValueError('unexpected extended attributes: ' + path)
    times = {}
    for field in ('atime', 'mtime', 'ctime', 'crtime'):
        match = re.search(r'\b' + field + r':\s*(0x[0-9a-f]+):([0-9a-f]+)', stat)
        if not match or int(match.group(2), 16):
            raise ValueError('unexpected timestamp precision: ' + path + ' ' + field)
        times[field] = match.group(1)
    with tempfile.TemporaryDirectory(prefix='ae-prop-') as d:
        file = Path(d)/'prop'
        editor.command(f'dump {path} {file}')
        content = file.read_bytes()
        editor.verify_file(path, hashlib.sha256(content).hexdigest(), 0o600, label)
    return content, times


def create_revision(source, output, expected_images, expected_props=None):
    source = Path(source).resolve(); output = Path(output).resolve()
    partial = output.with_name(output.name + '.partial')
    if output.exists() or partial.exists() or output.is_relative_to(source) or source.is_relative_to(output):
        raise ValueError('output exists or aliases source')
    original_bytes = (source/'integration.json').read_bytes()
    metadata = json.loads(original_bytes)
    if not isinstance(metadata, dict) or metadata.get('images') != expected_images:
        raise ValueError('source integration image hashes differ from expected')
    for name, sha in expected_images.items():
        if digest(source/name) != sha:
            raise ValueError('source image SHA mismatch: ' + name)
    replacements = {}
    for name, (path, label, changes) in PARTS.items():
        old, times = file_snapshot(Editor(source/name), path, label)
        if expected_props is not None and hashlib.sha256(old).hexdigest() != expected_props[name]:
            raise ValueError('source property SHA mismatch: ' + name)
        replacements[name] = (old, patch_properties(old, changes), times)
    partial.mkdir(parents=True)
    try:
        for name, sha in expected_images.items():
            print('Copying and hashing ' + name, flush=True)
            copy_image(source/name, partial/name, sha)
        revision_files = []
        for name, (path, label, _) in PARTS.items():
            old, new, times = replacements[name]
            editor = Editor(partial/name)
            with tempfile.TemporaryDirectory(prefix='ae-prop-revision-') as d:
                payload = Path(d)/'payload'; payload.write_bytes(new)
                editor.put(path, payload, 0o600, label, True)
            for field, value in times.items():
                editor.command(f'sif {path} {field} {value}', write=True)
            readback, saved_times = file_snapshot(editor, path, label)
            if readback != new or saved_times != times:
                raise ValueError('readback metadata/content mismatch: ' + name)
            revision_files.append({'image': name, 'target': path,
                'old_sha256': hashlib.sha256(old).hexdigest(), 'sha256': hashlib.sha256(new).hexdigest(),
                'mode': '0600', 'owner': '0:0', 'label': label.rstrip(b'\0').decode('ascii'),
                'timestamps': times})
        for name in PARTS:
            check = subprocess.run(['e2fsck', '-fn', str(partial/name)], stdout=subprocess.PIPE, stderr=subprocess.STDOUT, text=True)
            if check.returncode:
                raise ValueError('e2fsck failed for ' + name + ': ' + check.stdout[-2000:])
        metadata['images'] = {name: digest(partial/name) for name in expected_images}
        history = metadata.setdefault('revisions', [])
        if not isinstance(history, list): raise ValueError('existing revisions must be a list')
        history.append({'source_integration_sha256': hashlib.sha256(original_bytes).hexdigest(), 'files': revision_files})
        (partial/'integration.json').write_text(json.dumps(metadata, indent=2, ensure_ascii=False)+'\n', encoding='utf-8')
        partial.rename(output)
    except BaseException:
        shutil.rmtree(partial)
        raise
    return metadata


def main():
    p = argparse.ArgumentParser(description=__doc__)
    p.add_argument('--source', type=Path, default=ROOT/'build/rom-final-staging-v2')
    p.add_argument('--output', type=Path, default=ROOT/'build/rom-final-staging-v3')
    args = p.parse_args()
    metadata = create_revision(args.source, args.output, BASE_IMAGES, ORIGINAL_PROPS)
    for name, sha in metadata['images'].items(): print(name + ' SHA-256=' + sha)
    print('Independent revision ready; no ZIP or device modified.')


if __name__ == '__main__':
    try: main()
    except (OSError, ValueError, PatchError, json.JSONDecodeError) as exc:
        raise SystemExit('Error: ' + str(exc))
