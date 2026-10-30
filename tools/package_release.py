"""Package individual build images, documentation, third-party notices and font licenses."""
import argparse
import hashlib
import json
import re
import shutil
import subprocess
import zipfile
from pathlib import Path
from flash_release import IMAGES, validate_package
from release_provenance import verify_source_commit


def check_public_file(path, folder):
    relative = path.relative_to(folder)
    if not path.resolve().is_relative_to(folder.resolve()):
        raise RuntimeError('Package file resolves outside output directory: '+str(relative))
    parts = {part.casefold() for part in relative.parts}
    suffix, filename = path.suffix.casefold(), path.name.casefold()
    if parts.intersection({'private', 'backups', 'diagnostics'}) or suffix in ['.log','.elf','.map'] or filename.startswith('sdkconfig') or filename in ['nvs.bin', 'merged-binary.bin', 'before-full.bin', 'after-full.bin']:
        raise RuntimeError('Private/unreviewed file in package: '+str(relative))


root = Path(__file__).resolve().parents[1]
parser = argparse.ArgumentParser()
parser.add_argument('--version', required=True)
parser.add_argument('--build', type=Path, help='Use newly built images; omit to package the prepared release directory')
parser.add_argument('--source-commit', help='Exact release source commit corresponding to these build outputs')
parser.add_argument('--output-root', type=Path, default=root/'dist', help='Staging directory; use private/dist before acceptance')
parser.add_argument('--channel', choices=['development', 'preview'], help='Default for new packages: development')
parser.add_argument('--candidate-manifest', type=Path, help='Verify and identify a sealed candidate without rebuilding')
parser.add_argument('--full-image', type=Path, help='Already merged build image; verify blank gaps and copy without rebuilding')
parser.add_argument('--release-summary', help='Description independent of the firmware internal version')
parser.add_argument('--validation-summary', help='Actual device evidence and remaining acceptance limits')
parser.add_argument('--validation-report', help='Repository-relative, public docs/ or validation/ report')
args = parser.parse_args()
if not re.fullmatch(r'\d+\.\d+\.\d+', args.version):
    parser.error('Use a semantic release version, e.g. 1.0.0')
if args.source_commit and not re.fullmatch(r'[0-9a-fA-F]{40}', args.source_commit):
    parser.error('--source-commit must be an exact 40-character commit SHA')
if args.candidate_manifest and not args.build:
    parser.error('--candidate-manifest requires --build pointing at the sealed archive')
name = 'ai-passport-nabo-xiaozhi-radio-v'+args.version
output_root = args.output_root.resolve()
dest = output_root / name
for path in [dest, output_root/(name+'.zip'), output_root/(name+'-full.bin'),
             output_root/'manifest.json', output_root/'SHA256SUMS.txt']:
    if not path.resolve().is_relative_to(output_root):
        raise RuntimeError('Package artifact resolves outside output directory')
dest.mkdir(parents=True, exist_ok=True)
for path in dest.rglob('*'):
    check_public_file(path, dest)
sealed = None
source_commit_validation = None
if args.build:
    build = args.build.resolve()
    sources = {'bootloader.bin': 'bootloader/bootloader.bin',
               'partition-table.bin': 'partition_table/partition-table.bin',
               'xiaozhi.bin': 'xiaozhi.bin', 'assets.bin': 'generated_assets.bin'}
    if args.candidate_manifest:
        candidate_path = args.candidate_manifest.resolve()
        if candidate_path.parent != build:
            raise ValueError('Candidate manifest and --build must name the same sealed archive')
        sealed = json.loads(candidate_path.read_text('utf-8'))
        for relative, entry in sealed['files'].items():
            path = (build / relative).resolve()
            if not path.is_relative_to(build):
                raise ValueError('Unsafe candidate artifact path')
            data = path.read_bytes()
            if len(data) != entry['bytes'] or hashlib.sha256(data).hexdigest() != entry['sha256']:
                raise ValueError('Sealed candidate artifact changed: '+relative)
        patch = (build/'source.patch').read_bytes()
        if len(patch) != sealed['source_patch']['bytes'] or hashlib.sha256(patch).hexdigest() != sealed['source_patch']['sha256']:
            raise ValueError('Sealed candidate source patch changed')
        app = (build/'xiaozhi.bin').read_bytes()
        if app[32:36] != b'\x32\x54\xcd\xab' or app[176:208].hex() != sealed['files']['xiaozhi.elf']['sha256']:
            raise ValueError('Sealed candidate app/ELF identity differs')
        if args.source_commit:
            source_commit_validation = verify_source_commit(root, args.source_commit, sealed, build)
    images = []
    for filename, relative in sources.items():
        data = (build / relative).read_bytes()
        if sealed and (len(data) != sealed['files'][relative]['bytes'] or
                       hashlib.sha256(data).hexdigest() != sealed['files'][relative]['sha256']):
            raise ValueError('Sealed image changed during packaging: '+relative)
        (dest / filename).write_bytes(data)
        images.append({'file':filename, 'offset':hex(IMAGES[filename][0]), 'bytes':len(data),
                       'sha256':hashlib.sha256(data).hexdigest()})
    manifest = {'release_version':args.version, 'board':'ai-passport-nabo', 'chip':'esp32c3',
                'flash_bytes':0x800000, 'images':images, 'private_nvs_or_device_backup_included':False,
                'channel':args.channel or 'development', 'hardware_validated':False,
                'source_commit':args.source_commit or (None if sealed else subprocess.check_output(['git','rev-parse','HEAD'],cwd=root,text=True).strip())}
    if sealed:
        manifest.update(firmware_candidate=sealed['candidate'],
                        source_base_commit=sealed['source_base_commit'],
                        source_working_tree_dirty=sealed['source_working_tree_dirty'],
                        source_fingerprint_sha256=sealed['source_fingerprint_sha256'],
                        source_patch_sha256=sealed['source_patch']['sha256'],
                        candidate_manifest_sha256=hashlib.sha256(candidate_path.read_bytes()).hexdigest(),
                        debug_elf_sha256=sealed['files']['xiaozhi.elf']['sha256'],
                        app_internal_project_version=sealed.get('app_internal_project_version'),
                        internal_version_is_not_release_identity=True)
        if source_commit_validation:
            manifest['source_commit_validation'] = source_commit_validation
    (dest/'manifest.json').write_text(json.dumps(manifest, indent=2)+'\n', 'utf-8')
manifest, _ = validate_package(dest)
if manifest['release_version'] != args.version:
    raise RuntimeError('Directory version does not match manifest')
if args.channel:
    manifest['channel'] = args.channel
if args.source_commit:
    if not sealed and manifest.get('candidate_manifest_sha256'):
        raise ValueError('Binding a sealed package commit requires --build and --candidate-manifest')
    manifest['source_commit'] = args.source_commit
if args.release_summary:
    manifest['release_summary'] = args.release_summary
if args.validation_summary:
    manifest['validation_summary'] = args.validation_summary
if args.validation_report:
    relative = Path(args.validation_report)
    report = (root/relative).resolve()
    if relative.is_absolute() or '..' in relative.parts or not relative.parts or relative.parts[0] not in ['docs','validation'] or not report.is_relative_to(root) or not report.is_file():
        raise ValueError('Validation report must be an existing public docs/ or validation/ file')
    manifest['validation_report'] = relative.as_posix()
    manifest['validation_report_sha256'] = hashlib.sha256(report.read_bytes()).hexdigest()
full_target = None
full_source = args.full_image
if not full_source and manifest.get('full_image'):
    full_source = (output_root/manifest['full_image']['file']).resolve()
    if not full_source.is_relative_to(output_root):
        raise ValueError('Prepared full image must stay in the output directory')
if full_source:
    full = full_source.read_bytes()
    if sealed:
        entry = sealed['files']['full-build.bin']
        if len(full) != entry['bytes'] or hashlib.sha256(full).hexdigest() != entry['sha256']:
            raise ValueError('Full image differs from the sealed candidate')
    elif not args.full_image:
        entry = manifest['full_image']
        if len(full) != entry['bytes'] or hashlib.sha256(full).hexdigest() != entry['sha256']:
            raise ValueError('Prepared full image changed')
    cursor = 0
    ordered = sorted(validate_package(dest)[1].values(), key=lambda item:item[0])
    for offset, _, data in ordered:
        if full[cursor:offset] != b'\xff'*(offset-cursor) or full[offset:offset+len(data)] != data:
            raise ValueError('Full image has non-blank gaps or differs from packaged build images')
        cursor = offset+len(data)
    if len(full) != cursor:
        raise ValueError('Unexpected full-image tail')
    full_target = output_root/(name+'-full.bin')
    full_target.write_bytes(full)
    manifest['full_image'] = {'file':full_target.name, 'bytes':len(full),
                              'sha256':hashlib.sha256(full).hexdigest(),
                              'offset':'0x0', 'resets_user_data':True}
(dest/'manifest.json').write_text(json.dumps(manifest, indent=2)+'\n', 'utf-8')
for filename in ['LICENSE', 'THIRD_PARTY_NOTICES.md', 'requirements-flash.txt', 'CHANGELOG.md']:
    shutil.copy2(root/filename, dest/filename)
preview = manifest.get('channel') == 'preview'
notes = f'docs/RELEASE_FM_v{args.version}.md'
if not (root/notes).is_file():
    notes = 'docs/VALIDATION.md'
summary = manifest.get('release_summary') or ('这是公开体验固件，适配范围与已知限制见验证说明。' if preview else '此为开发测试构建，尚未完成电台真机验收。')
validation = manifest.get('validation_summary') or '构建、宿主测试与设备验收分别记录；不能用构建成功代替真实听感或长播验证。'
identity = ''
if manifest.get('firmware_candidate'):
    identity = f"封存候选：{manifest['firmware_candidate']}。"
if manifest.get('app_internal_project_version'):
    identity += f"发行版本为 v{args.version}；镜像内部版本仍为 {manifest['app_internal_project_version']}，用 manifest 中的应用 SHA256 识别精确固件。"
report_link = manifest.get('validation_report') or notes
(dest/'README.md').write_text(
    '# AI Passport NABO FM · '+('体验固件' if preview else '电台开发版')+' v'+args.version+' 刷写包\n\n'
    + summary+'\n\n'+validation+f' 详见 [{report_link}]({report_link})。\n\n'
    + (identity+'\n\n' if identity else '')
    +
    '适用 ESP32-C3 / 8 MB AI Passport。镜像校验和与偏移见 manifest.json。\n\n'
    '完整 BIN 从 0x0 写入会清空配网、激活和用户设置；需要保留数据时使用分区 ZIP 并先核对设备兼容性。\n\n'
    '先读 docs/FLASHING.md，按自己的端口和设备 MAC 执行：\n\n'
    '```powershell\npython -m pip install -r requirements-flash.txt\n'
    'python tools/flash_release.py --package . --mode inspect\n'
    '# 兼容布局的本定制版升级（写应用和资源，保留配网、激活、收藏和设置）：\n'
    'python tools/flash_release.py --package . --mode upgrade --port COM8 --expected-mac YOUR_DEVICE_MAC\n'
    '# 首次安装：将上面的 upgrade 改为 install\n```\n\n'
    '完整项目源码、使用和交接文档：<https://github.com/Liutupi/ai-passport-nabo-xiaozhi-radio>。\n'
    '备份留在 private/，请留在本机。第三方说明及字体许可随本包提供。\n', 'utf-8')
shutil.copytree(root/'docs', dest/'docs', dirs_exist_ok=True)
shutil.copytree(root/'validation', dest/'validation', dirs_exist_ok=True)
if manifest.get('validation_report'):
    packaged_report = (dest/manifest['validation_report']).read_bytes()
    if hashlib.sha256(packaged_report).hexdigest() != manifest['validation_report_sha256']:
        raise ValueError('Validation report changed while packaging')
shutil.copytree(root/'game-art', dest/'game-art', dirs_exist_ok=True, ignore=shutil.ignore_patterns('dive', 'table', '._*'))
(dest/'tools').mkdir(exist_ok=True)
shutil.copy2(root/'tools/flash_release.py', dest/'tools/flash_release.py')
(dest/'licenses').mkdir(exist_ok=True)
for font_license in ['OFL-LXGW.txt', 'OFL-SourceHanSans.txt', 'OFL-ArchivoBlack.txt', 'OFL-SpaceMono.txt']:
    shutil.copy2(root/'xiaozhi-esp32/main/boards/folotoy/ai-passport-nabo'/font_license, dest/'licenses'/font_license)
helix = root/'xiaozhi-esp32/main/boards/folotoy/ai-passport-nabo/helix'
shutil.copytree(helix, dest/'third_party/helix-mp3', dirs_exist_ok=True)
shutil.copy2(root/'xiaozhi-esp32/main/boards/folotoy/ai-passport-nabo/nabo_helix_buffers.c',
             dest/'third_party/helix-mp3/nabo_helix_buffers.c')
# macOS writes AppleDouble '._*' companions on non-APFS volumes; they never belong in a release.
for junk in [p for p in dest.rglob('._*')] + [p for p in dest.rglob('.DS_Store')]:
    junk.unlink() if junk.is_file() else shutil.rmtree(junk)
# Refuse accidental private files before archiving, even when reusing a directory.
files = sorted(p for p in dest.rglob('*') if p.is_file() and not p.name.startswith('._') and p.name != '.DS_Store')
for path in files:
    check_public_file(path, dest)
checksums = '\n'.join(hashlib.sha256(p.read_bytes()).hexdigest()+'  '+p.relative_to(dest).as_posix()
                      for p in files if p.name != 'SHA256SUMS.txt')+'\n'
(dest/'SHA256SUMS.txt').write_text(checksums, 'utf-8')
archive = output_root/(name+'.zip')
with zipfile.ZipFile(archive, 'w', zipfile.ZIP_DEFLATED) as z:
    for path in sorted(p for p in dest.rglob('*') if p.is_file() and not p.name.startswith('._')
                       and p.name != '.DS_Store'):
        z.write(path, path.relative_to(dest).as_posix())
outer = output_root/'SHA256SUMS.txt'
public_manifest = output_root/'manifest.json'
shutil.copy2(dest/'manifest.json', public_manifest)
outer.write_text('\n'.join(hashlib.sha256(p.read_bytes()).hexdigest()+'  '+p.name
                           for p in [archive, *([full_target] if full_target else []), public_manifest])+'\n', 'utf-8')
print('PASS: package validation; source/licenses included; no private device data')
print(archive)
