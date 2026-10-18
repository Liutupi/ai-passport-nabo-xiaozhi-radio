"""Package individual build images, documentation, third-party notices and font licenses."""
import argparse
import hashlib
import json
import re
import shutil
import zipfile
from pathlib import Path
from flash_release import IMAGES, validate_package

root = Path(__file__).resolve().parents[1]
parser = argparse.ArgumentParser()
parser.add_argument('--version', required=True)
parser.add_argument('--build', type=Path, help='Use newly built images; omit to package the prepared release directory')
args = parser.parse_args()
if not re.fullmatch(r'\d+\.\d+\.\d+', args.version):
    parser.error('Use a semantic release version, e.g. 1.0.0')
name = 'ai-passport-nabo-xiaozhi-table-v'+args.version
dest = root / 'dist' / name
dest.mkdir(parents=True, exist_ok=True)
if args.build:
    build = args.build.resolve()
    sources = {'bootloader.bin': 'bootloader/bootloader.bin',
               'partition-table.bin': 'partition_table/partition-table.bin',
               'xiaozhi.bin': 'xiaozhi.bin', 'assets.bin': 'generated_assets.bin'}
    images = []
    for filename, relative in sources.items():
        data = (build / relative).read_bytes()
        (dest / filename).write_bytes(data)
        images.append({'file':filename, 'offset':hex(IMAGES[filename][0]), 'bytes':len(data),
                       'sha256':hashlib.sha256(data).hexdigest()})
    manifest = {'release_version':args.version, 'board':'ai-passport-nabo', 'chip':'esp32c3',
                'flash_bytes':0x800000, 'images':images, 'private_nvs_or_device_backup_included':False}
    (dest/'manifest.json').write_text(json.dumps(manifest, indent=2)+'\n', 'utf-8')
manifest, _ = validate_package(dest)
if manifest['release_version'] != args.version:
    raise RuntimeError('Directory version does not match manifest')
for filename in ['LICENSE', 'THIRD_PARTY_NOTICES.md', 'requirements-flash.txt', 'CHANGELOG.md']:
    shutil.copy2(root/filename, dest/filename)
(dest/'README.md').write_text(
    '# AI Passport NABO 小智 · 牌桌版 v'+args.version+' 刷写包\n\n'
    '适用 ESP32-C3 / 8 MB AI Passport。镜像校验和与偏移见 manifest.json。\n\n'
    '先读 docs/FLASHING.md，按自己的端口和设备 MAC 执行：\n\n'
    '```powershell\npython -m pip install -r requirements-flash.txt\n'
    'python tools/flash_release.py --package . --mode inspect\n'
    '# 已安装本定制版（写应用和资源，保留配网、激活和成绩）：\n'
    'python tools/flash_release.py --package . --mode upgrade --port COM8 --expected-mac YOUR_DEVICE_MAC\n'
    '# 首次安装：将上面的 upgrade 改为 install\n```\n\n'
    '完整项目源码、使用和交接文档：<https://github.com/Liutupi/ai-passport-nabo-xiaozhi-table>。\n'
    '备份留在 private/，请留在本机。第三方说明及字体许可随本包提供。\n', 'utf-8')
shutil.copytree(root/'docs', dest/'docs', dirs_exist_ok=True)
shutil.copytree(root/'validation', dest/'validation', dirs_exist_ok=True)
shutil.copytree(root/'game-art', dest/'game-art', dirs_exist_ok=True, ignore=shutil.ignore_patterns('dive', '._*'))
(dest/'tools').mkdir(exist_ok=True)
shutil.copy2(root/'tools/flash_release.py', dest/'tools/flash_release.py')
(dest/'licenses').mkdir(exist_ok=True)
for font_license in ['OFL-SourceHanSans.txt', 'OFL-ArchivoBlack.txt', 'OFL-SpaceMono.txt']:
    shutil.copy2(root/'xiaozhi-esp32/main/boards/folotoy/ai-passport-nabo'/font_license, dest/'licenses'/font_license)
# macOS writes AppleDouble '._*' companions on non-APFS volumes; they never belong in a release.
for junk in [p for p in dest.rglob('._*')] + [p for p in dest.rglob('.DS_Store')]:
    junk.unlink() if junk.is_file() else shutil.rmtree(junk)
# Refuse accidental private files before archiving, even when reusing a directory.
files = sorted(p for p in dest.rglob('*') if p.is_file() and not p.name.startswith('._') and p.name != '.DS_Store')
for path in files:
    relative = path.relative_to(dest)
    if 'private' in relative.parts or path.suffix == '.log' or path.name in ['nvs.bin', 'merged-binary.bin']:
        raise RuntimeError('Private/unreviewed file in package: '+str(relative))
checksums = '\n'.join(hashlib.sha256(p.read_bytes()).hexdigest()+'  '+p.relative_to(dest).as_posix()
                      for p in files if p.name != 'SHA256SUMS.txt')+'\n'
(dest/'SHA256SUMS.txt').write_text(checksums, 'utf-8')
archive = root/'dist'/(name+'.zip')
with zipfile.ZipFile(archive, 'w', zipfile.ZIP_DEFLATED) as z:
    for path in sorted(p for p in dest.rglob('*') if p.is_file() and not p.name.startswith('._')
                       and p.name != '.DS_Store'):
        z.write(path, path.relative_to(dest).as_posix())
outer = root/'dist/SHA256SUMS.txt'
outer.write_text('\n'.join(hashlib.sha256(p.read_bytes()).hexdigest()+'  '+p.name
                           for p in [archive, dest/'xiaozhi.bin', dest/'assets.bin', dest/'manifest.json'])+'\n', 'utf-8')
print('PASS: package validation; source/licenses included; no private device data')
print(archive)
