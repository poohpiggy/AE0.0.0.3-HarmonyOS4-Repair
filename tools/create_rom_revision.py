#!/usr/bin/env python3
"""Create an independent ROM revision from hash-pinned images and file manifest.

Dry run by default. --apply creates a new work set, never a ZIP or device.
Manifest: {"files": [{"target": "/system/...", "source": "local path",
"sha256": "...", "old_sha256": "..." or null, "mode": "0777",
"label": "u:object_r:system_file:s0"}, ...]}. For replacements old mode,
owner root:root, SELinux label and content must all match; null means new.
"""
import argparse
import hashlib
import json
from pathlib import Path
import re
import shutil
import subprocess

from patch_images import Editor, PatchError, ROOT, digest, safe_target
from create_integrated_work import copy_image

IMAGE_NAMES = ('boot.img', 'system.img', 'vendor.img')
HEX = re.compile(r'^[a-f0-9]{64}$')
IMAGE_PATH = re.compile(r'^/system/(?:[A-Za-z0-9_.#-]+/)*[A-Za-z0-9_.#-]+$')
LABEL = re.compile(r'^u:object_r:[a-zA-Z0-9_]+:s0$')


def read_entries(manifest):
    data = json.loads(manifest.read_text(encoding='utf-8'))
    if not isinstance(data, dict) or set(data) != {'files'} or not isinstance(data['files'], list) or not data['files']:
        raise PatchError('manifest must contain a nonempty files list')
    result, seen = [], set()
    for item in data['files']:
        if not isinstance(item, dict) or set(item) != {'target','source','sha256','old_sha256','mode','label'}:
            raise PatchError('each entry requires target, source, sha256, old_sha256, mode, label')
        entry = dict(item)
        target = entry['target']
        if not isinstance(target, str) or not IMAGE_PATH.fullmatch(target) or safe_target(target) != target or target in seen:
            raise PatchError('unsafe or duplicate system target: ' + str(target))
        seen.add(target)
        if entry['mode'] not in ('0644','0755','0777'):
            raise PatchError('unsupported mode: ' + str(entry['mode']))
        if not isinstance(entry['label'], str) or not LABEL.fullmatch(entry['label']):
            raise PatchError('invalid label for ' + target)
        for name in ('sha256','old_sha256'):
            value=entry[name]
            if name == 'old_sha256' and value is None: continue
            if not isinstance(value,str) or not HEX.fullmatch(value): raise PatchError('invalid ' + name + ' for ' + target)
        if not isinstance(entry['source'],str) or not entry['source']:
            raise PatchError('invalid source for ' + target)
        source = Path(entry['source'])
        if not source.is_absolute(): source = ROOT / source
        source = source.resolve()
        if not source.is_file() or digest(source) != entry['sha256']:
            raise PatchError('source missing or SHA mismatch: ' + str(source))
        entry['source'] = str(source)
        result.append(entry)
    return result


def preflight(editor, entries):
    for entry in entries:
        target = entry['target']
        if entry['old_sha256'] is None:
            if editor.stat(target): raise PatchError('new target already exists: ' + target)
        else:
            editor.verify_file(target, entry['old_sha256'], int(entry['mode'],8), entry['label'].encode()+b'\0')


def apply(editor, entries):
    preflight(editor, entries)
    for e in entries:
        editor.put(e['target'], Path(e['source']), int(e['mode'],8), e['label'].encode()+b'\0', e['old_sha256'] is not None)
    for e in entries:
        editor.verify_file(e['target'], e['sha256'], int(e['mode'],8), e['label'].encode()+b'\0')


def main():
    p=argparse.ArgumentParser(description=__doc__)
    p.add_argument('--source',type=Path,default=ROOT/'build/rom-final-staging')
    p.add_argument('--output',type=Path,required=True)
    p.add_argument('--manifest',type=Path,required=True)
    p.add_argument('--apply',action='store_true')
    args=p.parse_args()
    source=args.source.resolve(); output=args.output.resolve(); partial=output.with_name(output.name+'.partial')
    if output.exists() or partial.exists() or output.is_relative_to(source) or source.is_relative_to(output):
        raise PatchError('output exists or aliases source')
    metadata_path=source/'integration.json'
    original_bytes=metadata_path.read_bytes(); metadata=json.loads(original_bytes)
    images=metadata.get('images') if isinstance(metadata,dict) else None
    if not isinstance(images,dict) or set(images)!=set(IMAGE_NAMES) or any(not isinstance(v,str) or not HEX.fullmatch(v) for v in images.values()):
        raise PatchError('invalid integration.json images mapping')
    entries=read_entries(args.manifest)
    for name in IMAGE_NAMES:
        if not (source/name).is_file() or digest(source/name)!=images[name]: raise PatchError('source image SHA mismatch: '+name)
    preflight(Editor(source/'system.img'), entries)
    print('Preflight passed; source integration SHA-256=' + hashlib.sha256(original_bytes).hexdigest(),flush=True)
    if not args.apply:
        print('Dry run only; no output created.'); return
    partial.mkdir(parents=True)
    try:
        for name in IMAGE_NAMES:
            print('Copying and hashing '+name,flush=True)
            copy_image(source/name,partial/name,images[name])
        apply(Editor(partial/'system.img'),entries)
        for name in ('system.img','vendor.img'):
            check=subprocess.run(['e2fsck','-fn',str(partial/name)],stdout=subprocess.PIPE,stderr=subprocess.STDOUT,text=True)
            if check.returncode: raise PatchError('e2fsck failed for '+name+': '+check.stdout[-2000:])
        metadata['images']={name:digest(partial/name) for name in IMAGE_NAMES}
        history=metadata.setdefault('revisions',[])
        if not isinstance(history,list): raise PatchError('existing revisions must be a list')
        history.append({'source_integration_sha256':hashlib.sha256(original_bytes).hexdigest(),'files':entries})
        (partial/'integration.json').write_text(json.dumps(metadata,indent=2,ensure_ascii=False)+'\n',encoding='utf-8')
        partial.rename(output)
    except BaseException:
        shutil.rmtree(partial)
        raise
    print('Independent revision ready: '+str(output))
    for name in IMAGE_NAMES: print(name+' SHA-256='+metadata['images'][name])
    print('No package created or device imported.')

if __name__=='__main__':
    try: main()
    except (OSError, PatchError, ValueError, json.JSONDecodeError) as exc: raise SystemExit('Error: '+str(exc))
