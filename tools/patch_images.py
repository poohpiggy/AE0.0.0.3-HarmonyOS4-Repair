#!/usr/bin/env python3
"""Inspect and apply a separately approved, verified ROM image patch plan."""
import argparse
import hashlib
import re
import shutil
import subprocess
import tempfile
import sys
from pathlib import Path, PurePosixPath


class PatchError(RuntimeError): pass


def digest(path):
    h=hashlib.sha256()
    with Path(path).open('rb') as f:
        for chunk in iter(lambda:f.read(1024*1024),b''): h.update(chunk)
    return h.hexdigest()


def safe_target(path):
    p=PurePosixPath(path)
    if not path.startswith('/') or any(c.isspace() or c in '"\\' for c in path) or '..' in p.parts:
        raise PatchError('unsafe image path: '+path)
    return str(p)


class Editor:
    def __init__(self, image): self.image=Path(image)

    def command(self, command, write=False, missing_ok=False):
        args=['debugfs']+(['-w'] if write else [])+['-R',command,str(self.image)]
        p=subprocess.run(args,text=True,stdout=subprocess.PIPE,stderr=subprocess.STDOUT)
        output=p.stdout
        bad=re.search(r'(?i)(file not found|not found by ext2_lookup|no such file|permission denied|already exists|ext2fs_.*error|filesystem full|could not allocate|usage:|invalid argument|failed|out of space)',output)
        if p.returncode or (bad and not (missing_ok and ('not found' in bad.group().lower() or 'no such file' in bad.group().lower()))):
            raise PatchError(f'debugfs {command}: {output.strip()}')
        return output

    def stat(self,path):
        output=self.command('stat '+safe_target(path),missing_ok=True)
        return output if re.search(r'\bInode:\s*\d+',output) else None

    def ensure_dir(self,path,label=None):
        path=safe_target(path)
        if label is None: label=SYSTEM_LABEL
        current=''
        for part in PurePosixPath(path).parts[1:]:
            current+='/'+part
            info=self.stat(current)
            if info is None:
                self.command('mkdir '+current,write=True)
                self.command('sif '+current+' mode 040755',write=True)
                self.command('sif '+current+' uid 0',write=True)
                self.command('sif '+current+' gid 0',write=True)
                with tempfile.TemporaryDirectory(prefix='ae-dir-') as d:
                    expected=Path(d)/'label';actual=Path(d)/'actual';expected.write_bytes(label)
                    self.command(f'ea_set -f {expected} {current} security.selinux',write=True)
                    self.command(f'ea_get -f {actual} {current} security.selinux')
                    if not actual.is_file() or actual.read_bytes()!=label: raise PatchError('directory label mismatch: '+current)
                info=self.stat(current)
                if not info or not re.search(r'\bMode:\s*0755\b',info): raise PatchError('directory mode mismatch: '+current)
            if not info or 'Type: directory' not in info: raise PatchError('not a directory: '+current)

    def put(self,path,source,mode,label,replace):
        path=safe_target(path); existing=self.stat(path)
        if bool(existing)!=bool(replace): raise PatchError(f'unexpected existing state for {path}')
        self.ensure_dir(str(PurePosixPath(path).parent),VENDOR_CONFIG_LABEL if label in (VENDOR_LABEL,VENDOR_CONFIG_LABEL) else SYSTEM_LABEL)
        if existing: self.command('rm '+path,write=True)
        with tempfile.TemporaryDirectory(prefix='ae-img-') as d:
            input_path=Path(d)/'payload';shutil.copyfile(source,input_path)
            label_path=Path(d)/'label';label_path.write_bytes(label)
            self.command(f'write {input_path} {path}',write=True)
            self.command(f'sif {path} mode 010{mode:04o}',write=True)
            self.command(f'sif {path} uid 0',write=True)
            self.command(f'sif {path} gid 0',write=True)
            self.command(f'ea_set -f {label_path} {path} security.selinux',write=True)
        self.verify_file(path,digest(source),mode,label)

    def verify_file(self,path,expected,mode,label):
        info=self.stat(path)
        if not info or 'Type: regular' not in info or not re.search(r'\bMode:\s*'+f'{mode:04o}'+r'\b',info):
            raise PatchError('unexpected inode or mode: '+path)
        if not re.search(r'\bUser:\s*0\s+Group:\s*0\b',info): raise PatchError('unexpected owner: '+path)
        with tempfile.TemporaryDirectory(prefix='ae-check-') as d:
            content=Path(d)/'content'; attr=Path(d)/'attr'
            self.command(f'dump {path} {content}')
            self.command(f'ea_get -f {attr} {path} security.selinux')
            if not content.is_file() or digest(content)!=expected: raise PatchError('hash mismatch: '+path)
            if not attr.is_file() or attr.read_bytes()!=label: raise PatchError('SELinux attribute mismatch: '+path)

    def symlink(self,path,target):
        path=safe_target(path)
        if not target or any(c.isspace() or c in '"\\' for c in target): raise PatchError('unsafe link target')
        if self.stat(path): raise PatchError('link path exists: '+path)
        self.ensure_dir(str(PurePosixPath(path).parent))
        self.command(f'symlink {path} {target}',write=True)
        self.verify_symlink(path,target)

    def verify_symlink(self,path,target):
        info=self.stat(path)
        if not info or 'Type: symlink' not in info or f'Fast link dest: "{target}"' not in info:
            raise PatchError('unexpected symlink: '+path)


ROOT=Path(__file__).resolve().parent.parent
SYSTEM_LABEL=b'u:object_r:system_file:s0\0'
LIB_LABEL=b'u:object_r:system_lib_file:s0\0'
VENDOR_LABEL=b'u:object_r:vendor_file:s0\0'
VENDOR_CONFIG_LABEL=b'u:object_r:vendor_configs_file:s0\0'

# (image, image path, local source, source sha256, mode, label, original sha256)
FILES=[
 ('system','/system/lib64/libsensor.so','diagnostics/libsensor-patched.so','f518d588889037af0a2b7f67d08e911c4b852fac06620ad38728aefc3631a8a0',0o644,LIB_LABEL,'5fa87c0d343ac28907c6ee4f3293f7170726b0eca6824395dc1600baf1ca8450'),
 ('system','/system/etc/classpaths/bootclasspath.pb','diagnostics/mdm-compat/bootclasspath-patched.pb','867bc2631f26be73bfd2e34723db12d1d75c032b4f87f6ed9a4fffb7be385a0d',0o644,SYSTEM_LABEL,'035c511e9754f21e9ad6f141fbaf29f0dbcb598044300a61bce2d14afc2523af'),
 ('system','/system/framework/codex-mdm-compat.jar','diagnostics/mdm-compat/mdm-compat.jar','fe708389b0333d4629c945691c8250590c310fa242065ac17020121a9ee07bff',0o644,SYSTEM_LABEL,None),
 ('system','/system/etc/init/00-codex-harmony.rc','diagnostics/firstboot/00-codex-harmony.rc','e5bfe085aa42f775fa38cecd3698c8ebb9ea3f8460a0aaa0b47a582a624584da',0o644,SYSTEM_LABEL,None),
 ('system','/system/etc/codex-harmony-start','diagnostics/firstboot/codex-harmony-start','74998e0ac38f0cbf30c413dfb0f65fbcdb599a1a31401c33dde84a0801585220',0o755,SYSTEM_LABEL,None),
 ('system','/system/etc/codex-harmony-firstboot','diagnostics/firstboot/codex-harmony-firstboot','3b17e17bd75a5fb391c82b767f8ee4b2656999608d9b2452e76a5e7ddec69054',0o755,SYSTEM_LABEL,None),
 ('system','/system/etc/codex/harmonybridge.apk','diagnostics/host-probe/probe.apk','59af18736c8ccac1e8fdcfe753794e05a2303ee042798d20bbf24acee05a2a20',0o644,SYSTEM_LABEL,None),
 ('system','/system/etc/codex/allow-theme-storage','diagnostics/firstboot/opt-in-marker','e3b0c44298fc1c149afbf4c8996fb92427ae41e4649b934ca495991b7852b855',0o644,SYSTEM_LABEL,None),
 ('system','/system/etc/codex/disable-theme-preview','diagnostics/firstboot/opt-in-marker','e3b0c44298fc1c149afbf4c8996fb92427ae41e4649b934ca495991b7852b855',0o644,SYSTEM_LABEL,None),
 ('system','/system/.aemu-parked/vendor#etc#vintf#manifest#codex-ae-radio-v1.xml','diagnostics/radio-bridge/packaging/parked-placeholder','a2ef93f67bed0e93801ab03d67c6e91589bf1a0cc45b3b17688803901d2f7bd5',0o644,SYSTEM_LABEL,None),
 ('vendor','/lib64/libcodex-ae-ril.so','build/verified-inputs/libcodex-ae-ril.so','458d17a0a8c7af14761bbab562b608f26817648ddf19e66cf6434b71304d138a',0o755,VENDOR_LABEL,None),
 ('vendor','/etc/vintf/manifest/codex-ae-radio-v1.xml','diagnostics/radio-bridge/packaging/codex-ae-radio-v1.xml','60bfe55294e1a9fffc6f204e5c4ceb9b4b03cbb843e70a71a991168527d53530',0o644,VENDOR_CONFIG_LABEL,None),
]
RADIO_SOURCE='diagnostics/radio-bridge/packaging/codex-ae-radio.rc'
RADIO_HASH='8e9c8d0d6f88dc4523a948e84fa0c1f616f22b8e30dd743a254362661d9ed9ef'
AUDIO_FILE=('system','/system/lib64/libaudiopolicyenginedefault.so','diagnostics/radio-bridge/audio/libaudiopolicyenginedefault.candidate.so','66aa92bf0811203055934ef33600942baecf70c55092518712e939c682eaa54a',0o644,LIB_LABEL,'c9b0f2c175e175d6ba3e202a3333fa5801be417bd1888a5c62b33a9dfe4baaf6')
LINKS=[('system','/system/bin/codex-harmony-sh','sh'),('vendor','/bin/hw/codex-ae-rild','rild')]
IMAGE_HASHES={'system':'aca78541de8308c52b3b27ad6e33460d106dc0c4525f444e8aa70cf713c62dd2',
              'vendor':'be780b43a9b1264b9e29d0d096ea6a50e3f97f94fbc74fab6f8d32149c0f805f'}


def original_hash(editor,path):
    if not editor.stat(path): raise PatchError('missing original: '+path)
    with tempfile.TemporaryDirectory(prefix='ae-original-') as d:
        out=Path(d)/'original';editor.command(f'dump {safe_target(path)} {out}')
        if not out.is_file(): raise PatchError('could not dump '+path)
        return digest(out)


def main():
    p=argparse.ArgumentParser(description=__doc__)
    p.add_argument('--staging',type=Path,default=ROOT/'build/staging')
    p.add_argument('--apply',action='store_true',help='modify verified staging images')
    p.add_argument('--audio-verified',action='store_true',help='install audio candidate after independent device verification')
    p.add_argument('--enable-radio',action='store_true',help='enable radio at boot; requires --audio-verified')
    args=p.parse_args()
    if args.enable_radio and not args.audio_verified: raise PatchError('enabling radio requires verified audio')
    editors={name:Editor(args.staging/(name+'.img')) for name in IMAGE_HASHES}
    for name,editor in editors.items():
        original=ROOT/'build/ota-unpacked'/(name+'.img')
        if not editor.image.is_file() or editor.image.samefile(original): raise PatchError('staging image missing or aliases original')
        print('Checking image:',editor.image,flush=True)
        if digest(editor.image)!=IMAGE_HASHES[name]: raise PatchError('unexpected staging image checksum: '+str(editor.image))
    entries=FILES.copy()
    radio_path='/etc/init/codex-ae-radio.rc' if args.enable_radio else '/etc/init/codex-ae-radio.rc.codex-disabled'
    entries.append(('vendor',radio_path,RADIO_SOURCE,RADIO_HASH,0o644,VENDOR_CONFIG_LABEL,None))
    if args.audio_verified: entries.append(AUDIO_FILE)
    for image,path,source,sha,mode,label,original in entries:
        if digest(ROOT/source)!=sha: raise PatchError('input checksum mismatch: '+source)
        found=editors[image].stat(path)
        if original:
            if not found or original_hash(editors[image],path)!=original: raise PatchError('original content mismatch: '+path)
        elif found: raise PatchError('new target already exists: '+path)
    # Always validate the original audio even when it is not being modified.
    if original_hash(editors['system'],AUDIO_FILE[1])!=AUDIO_FILE[-1]: raise PatchError('original audio mismatch')
    for image,path,target in LINKS:
        if editors[image].stat(path): raise PatchError('link already exists: '+path)
    print('Plan (paths are partition-relative):')
    for image,path,source,sha,mode,label,original in entries:print(f'  {image}:{path} <- {source} sha256={sha} mode={mode:04o}')
    for image,path,target in LINKS:print(f'  {image}:{path} -> {target}')
    if not args.apply:
        print('Preflight only; staging has not been modified.');return
    for image,path,source,sha,mode,label,original in entries:
        editors[image].put(path,ROOT/source,mode,label,replace=original is not None)
    for image,path,target in LINKS:editors[image].symlink(path,target)
    for name,editor in editors.items():
        print('Checking filesystem:',editor.image,flush=True)
        subprocess.run(['e2fsck','-fn',str(editor.image)],check=True)
        print(f'{name}: sha256={digest(editor.image)}',flush=True)
    print('Patch and read-back checks complete. Import and runtime are not yet verified.')


if __name__=='__main__':
    try:main()
    except (PatchError,OSError,subprocess.CalledProcessError) as e:
        print('Patch stopped:',e,file=sys.stderr);sys.exit(1)
