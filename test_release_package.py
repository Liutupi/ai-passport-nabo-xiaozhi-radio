"""Reject corrupt/unsafe install packages without touching a serial port."""
import json
import hashlib
import struct
import subprocess
import sys
import tempfile
import unittest
import zipfile
from pathlib import Path
from tools.flash_release import IMAGES, PARTITIONS, digest, validate_package
from tools.release_provenance import verify_source_commit
PROJECT = Path(__file__).resolve().parent

class ReleasePackageTests(unittest.TestCase):
    def setUp(self):
        self.temp = tempfile.TemporaryDirectory()
        self.addCleanup(self.temp.cleanup)
        self.root = Path(self.temp.name)
        table = b''.join(struct.pack('<HBBII16sI', 0x50aa, kind, subtype, offset, size,
                                     name.encode().ljust(16,b'\0'), 0)
                         for name, (kind, subtype, offset, size) in PARTITIONS.items()) + b'\xff'*32
        self.manifest = {'release_version': '1.0.0', 'board':'ai-passport-nabo', 'chip':'esp32c3',
                         'flash_bytes':0x800000, 'images':[]}
        for name, (offset, _) in IMAGES.items():
            data = table if name == 'partition-table.bin' else b'\xe9'+bytes(range(64))
            (self.root/name).write_bytes(data)
            self.manifest['images'].append({'file':name, 'offset':hex(offset), 'bytes':len(data), 'sha256':digest(data)})
        self.save()

    def save(self):
        (self.root/'manifest.json').write_text(json.dumps(self.manifest))

    def test_valid_package(self):
        self.assertEqual(len(validate_package(self.root)[1]), 4)

    def test_corrupt_binary(self):
        (self.root/'xiaozhi.bin').write_bytes(b'\xe9broken')
        with self.assertRaises(ValueError): validate_package(self.root)

    def test_path_traversal_is_rejected(self):
        self.manifest['images'][2]['file'] = '../xiaozhi.bin'; self.save()
        with self.assertRaises(ValueError): validate_package(self.root)

    def test_write_to_nvs_is_rejected(self):
        self.manifest['images'][2]['offset'] = '0x9000'; self.save()
        with self.assertRaises(ValueError): validate_package(self.root)

    def test_wrong_board_is_rejected(self):
        self.manifest['chip'] = 'esp32s3'; self.save()
        with self.assertRaises(ValueError): validate_package(self.root)

    def test_overlarge_app_is_rejected(self):
        data = b'\xe9'+b'\0'*0x3f0000
        (self.root/'xiaozhi.bin').write_bytes(data)
        self.manifest['images'][2].update(bytes=len(data),sha256=digest(data)); self.save()
        with self.assertRaises(ValueError): validate_package(self.root)

    def test_changed_partition_is_rejected_even_with_valid_hash(self):
        path = self.root/'partition-table.bin'
        data = bytearray(path.read_bytes()); struct.pack_into('<I', data, 4, 0xa000)
        path.write_bytes(data)
        self.manifest['images'][1]['sha256'] = digest(data); self.save()
        with self.assertRaises(ValueError): validate_package(self.root)

    def sealed_build(self):
        """A tiny offline archive with independently bound app/ELF and FF gaps."""
        build = self.root/'sealed'
        names = {'bootloader.bin':'bootloader/bootloader.bin',
                 'partition-table.bin':'partition_table/partition-table.bin',
                 'xiaozhi.bin':'xiaozhi.bin', 'assets.bin':'generated_assets.bin'}
        for public, relative in names.items():
            path = build/relative
            path.parent.mkdir(parents=True, exist_ok=True)
            path.write_bytes((self.root/public).read_bytes())
        elf = b'fixture ELF identity for packaging, not firmware'
        (build/'xiaozhi.elf').write_bytes(elf)
        app = bytearray(b'\xe9'+b'\0'*255)
        app[32:36] = b'\x32\x54\xcd\xab'
        app[176:208] = hashlib.sha256(elf).digest()
        (build/'xiaozhi.bin').write_bytes(app)
        patch = b'fixture source patch\n'
        (build/'source.patch').write_bytes(patch)
        full = bytearray(b'\xff'*(0x400000+len((build/'generated_assets.bin').read_bytes())))
        for public, relative in names.items():
            offset = IMAGES[public][0]
            data = (build/relative).read_bytes()
            full[offset:offset+len(data)] = data
        (build/'full-build.bin').write_bytes(full)
        manifest = {'candidate':'NABO FM 06r2', 'source_base_commit':'1'*40,
                    'source_working_tree_dirty':True, 'source_fingerprint_sha256':'2'*64,
                    'app_internal_project_version':'2.5.0',
                    'source_patch':{'bytes':len(patch),'sha256':digest(patch)}, 'files':{}}
        for relative in [*names.values(), 'xiaozhi.elf', 'full-build.bin']:
            data = (build/relative).read_bytes()
            manifest['files'][relative] = {'bytes':len(data),'sha256':digest(data)}
        (build/'manifest.json').write_text(json.dumps(manifest))
        return build

    def run_packager(self, build, *extra):
        return subprocess.run(
            [sys.executable, str(PROJECT/'tools/package_release.py'), '--version', '1.0.1',
             '--build', str(build), '--candidate-manifest', str(build/'manifest.json'),
             '--full-image', str(build/'full-build.bin'), '--output-root', str(self.root/'out'),
             '--channel', 'preview', '--release-summary', '候选06r2体验更新。',
             '--validation-summary', '宿主回归通过；设备长播验收仍在进行。',
             '--validation-report', 'docs/VALIDATION.md', *extra],
            capture_output=True, text=True, encoding='utf-8', errors='replace')

    def test_preview_update_keeps_exact_images_and_separates_version_and_evidence(self):
        build = self.sealed_build()
        result = self.run_packager(build)
        self.assertEqual(result.returncode, 0, result.stdout+result.stderr)
        dest = self.root/'out/ai-passport-nabo-xiaozhi-radio-v1.0.1'
        manifest, images = validate_package(dest)
        self.assertEqual(images['xiaozhi.bin'][2], (build/'xiaozhi.bin').read_bytes())
        self.assertEqual(manifest['channel'], 'preview')
        self.assertFalse(manifest['hardware_validated'])
        self.assertIsNone(manifest['source_commit'], 'Uncommitted candidate must not claim current HEAD as its full source')
        self.assertEqual(manifest['source_fingerprint_sha256'], '2'*64)
        self.assertEqual(manifest['debug_elf_sha256'], digest((build/'xiaozhi.elf').read_bytes()))
        readme = (dest/'README.md').read_text('utf-8')
        self.assertIn('v1.0.1', readme)
        self.assertIn('2.5.0', readme)
        self.assertIn('设备长播验收仍在进行', readme)
        self.assertIn('会清空配网', readme)
        self.assertNotIn('候选 03', readme)
        self.assertNotIn('RELEASE_FM_v1.0.0', readme)
        full = self.root/'out'/manifest['full_image']['file']
        self.assertEqual(full.read_bytes(), (build/'full-build.bin').read_bytes())
        self.assertTrue(manifest['full_image']['resets_user_data'])
        self.assertEqual((self.root/'out/manifest.json').read_bytes(), (dest/'manifest.json').read_bytes())
        checksums = (self.root/'out/SHA256SUMS.txt').read_text().splitlines()
        self.assertEqual(len(checksums), 3)
        for line in checksums:
            expected, filename = line.split('  ', 1)
            self.assertEqual(digest((self.root/'out'/filename).read_bytes()), expected)
        with zipfile.ZipFile(self.root/'out/ai-passport-nabo-xiaozhi-radio-v1.0.1.zip') as archive:
            self.assertEqual(archive.read('xiaozhi.bin'), (build/'xiaozhi.bin').read_bytes())
            self.assertIn('licenses/OFL-LXGW.txt', archive.namelist())
            self.assertFalse(any(name.endswith(('.elf','.map','.log')) or '/private/' in name
                                 for name in archive.namelist()))
        # A prepared package cannot bind an arbitrary commit without returning
        # to its sealed source snapshot for production-source verification.
        result = subprocess.run([sys.executable, str(PROJECT/'tools/package_release.py'),
                                 '--version', '1.0.1', '--output-root', str(self.root/'out'),
                                 '--source-commit', '4'*40], capture_output=True, text=True,
                                encoding='utf-8', errors='replace')
        self.assertNotEqual(result.returncode, 0)
        self.assertIn('requires --build and --candidate-manifest', result.stderr)
        updated, updated_images = validate_package(dest)
        self.assertIsNone(updated['source_commit'])
        self.assertEqual(updated['firmware_candidate'], 'NABO FM 06r2')
        self.assertEqual(updated_images['xiaozhi.bin'][2], (build/'xiaozhi.bin').read_bytes())
        self.assertEqual(full.read_bytes(), (build/'full-build.bin').read_bytes())
        self.assertEqual(len((self.root/'out/SHA256SUMS.txt').read_text().splitlines()), 3)

    def test_changed_sealed_candidate_is_rejected_before_packaging(self):
        build = self.sealed_build()
        (build/'xiaozhi.bin').write_bytes(b'\xe9changed')
        result = self.run_packager(build)
        self.assertNotEqual(result.returncode, 0)
        self.assertIn('Sealed candidate artifact changed', result.stderr)
        self.assertFalse((self.root/'out/ai-passport-nabo-xiaozhi-radio-v1.0.1.zip').exists())

    def test_full_image_containing_private_nvs_is_rejected_even_with_matching_archive_hash(self):
        build = self.sealed_build()
        path = build/'full-build.bin'
        full = bytearray(path.read_bytes()); full[0x9000] = 0x12; path.write_bytes(full)
        manifest = json.loads((build/'manifest.json').read_text())
        manifest['files']['full-build.bin']['sha256'] = digest(full)
        (build/'manifest.json').write_text(json.dumps(manifest))
        result = self.run_packager(build)
        self.assertNotEqual(result.returncode, 0)
        self.assertIn('non-blank gaps', result.stderr)
        self.assertFalse((self.root/'out/ai-passport-nabo-xiaozhi-radio-v1.0.1-full.bin').exists())

    def test_stale_debug_elf_is_not_shipped(self):
        build = self.sealed_build()
        dest = self.root/'out/ai-passport-nabo-xiaozhi-radio-v1.0.1'
        dest.mkdir(parents=True)
        (dest/'xiaozhi.elf').write_bytes(b'private debug data')
        result = self.run_packager(build)
        self.assertNotEqual(result.returncode, 0)
        self.assertIn('Private/unreviewed file', result.stderr)
        self.assertFalse((self.root/'out/ai-passport-nabo-xiaozhi-radio-v1.0.1.zip').exists())

    def test_dirty_candidate_base_cannot_be_claimed_as_complete_source(self):
        build = self.sealed_build()
        result = self.run_packager(build, '--source-commit', '1'*40)
        self.assertNotEqual(result.returncode, 0)
        self.assertIn('Dirty sealed candidate cannot use its base commit', result.stderr)
        self.assertFalse((self.root/'out/ai-passport-nabo-xiaozhi-radio-v1.0.1.zip').exists())

    def test_private_file_names_are_rejected_without_case_sensitivity(self):
        build = self.sealed_build()
        dest = self.root/'out/ai-passport-nabo-xiaozhi-radio-v1.0.1'
        dest.mkdir(parents=True)
        for name in ['NVS.BIN', 'XIAOZHI.ELF', 'DEBUG.LOG', 'SDKCONFIG.old', 'Private/device.txt']:
            with self.subTest(name=name):
                path = dest/name
                path.parent.mkdir(parents=True, exist_ok=True)
                path.write_bytes(b'private fixture')
                result = self.run_packager(build)
                self.assertNotEqual(result.returncode, 0)
                self.assertIn('Private/unreviewed file', result.stderr)
                self.assertFalse((self.root/'out/ai-passport-nabo-xiaozhi-radio-v1.0.1.zip').exists())
                path.unlink()
                if path.parent != dest:
                    path.parent.rmdir()

    def test_link_to_file_outside_package_is_rejected_before_writing(self):
        build = self.sealed_build()
        dest = self.root/'out/ai-passport-nabo-xiaozhi-radio-v1.0.1'
        dest.mkdir(parents=True)
        external = self.root/'external.bin'
        external.write_bytes(b'untouched external data')
        try:
            (dest/'xiaozhi.bin').symlink_to(external)
        except (OSError, NotImplementedError):
            self.skipTest('File symlinks unavailable on this host')
        result = self.run_packager(build)
        self.assertNotEqual(result.returncode, 0)
        self.assertIn('outside output directory', result.stderr)
        self.assertEqual(external.read_bytes(), b'untouched external data')


class SourceProvenanceTests(unittest.TestCase):
    def setUp(self):
        self.temp = tempfile.TemporaryDirectory()
        self.addCleanup(self.temp.cleanup)
        self.folder = Path(self.temp.name)
        self.repo = self.folder/'repo'
        self.repo.mkdir()
        self.git('init', '-q')
        self.git('config', 'user.name', 'Release provenance test')
        self.git('config', 'user.email', 'release-test@example.invalid')
        self.git('config', 'core.autocrlf', 'false')
        self.git('config', 'commit.gpgsign', 'false')
        self.git('config', 'core.hooksPath', str(self.folder/'no-hooks'))
        self.code = self.repo/'xiaozhi-esp32/main/radio.c'
        self.code.parent.mkdir(parents=True)
        self.code.write_bytes(b'int radio(void) { return 1; }\n')
        self.config = self.repo/'configs/release.sdkconfig'
        self.config.parent.mkdir()
        self.config.write_bytes(b'CONFIG_BOARD_FM=y\n')
        self.base = self.commit('base')
        self.code.write_bytes(b'int radio(void) { return 2; }\n')
        self.fixed = self.commit('firmware source')
        self.archive = self.folder/'archive'
        self.sealed = {'source_base_commit':self.base, 'source_working_tree_dirty':True,
                       'source_files':{}}
        for path in [self.code, self.config]:
            relative = path.relative_to(self.repo).as_posix()
            # Model the real Windows checkout / normalized Git-blob boundary.
            data = path.read_bytes().replace(b'\n', b'\r\n')
            target = self.archive/'source-state'/relative
            target.parent.mkdir(parents=True, exist_ok=True)
            target.write_bytes(data)
            self.sealed['source_files'][relative] = {'bytes':len(data), 'sha256':digest(data)}

    def git(self, *args):
        result = subprocess.run(['git', *args], cwd=self.repo, capture_output=True, check=True)
        return result.stdout.decode().strip()

    def commit(self, message):
        self.git('add', '.')
        self.git('commit', '-q', '-m', message)
        return self.git('rev-parse', 'HEAD')

    def test_real_docs_commit_with_identical_production_snapshot_is_accepted(self):
        (self.repo/'README.md').write_text('Updated release notes\n')
        final = self.commit('release documentation')
        checked = verify_source_commit(self.repo, final, self.sealed, self.archive)
        self.assertEqual(checked['status'], 'PASS')
        self.assertEqual(checked['production_files'], 2)

    def test_dirty_base_commit_is_rejected(self):
        with self.assertRaisesRegex(ValueError, 'Dirty sealed candidate'):
            verify_source_commit(self.repo, self.base, self.sealed, self.archive)

    def test_nonexistent_commit_is_rejected(self):
        with self.assertRaises(ValueError):
            verify_source_commit(self.repo, 'f'*40, self.sealed, self.archive)

    def test_existing_commit_with_different_production_source_is_rejected(self):
        self.code.write_bytes(b'int radio(void) { return 3; }\n')
        wrong = self.commit('different firmware')
        with self.assertRaisesRegex(ValueError, 'differs from sealed production source'):
            verify_source_commit(self.repo, wrong, self.sealed, self.archive)

    def test_unrecorded_production_change_is_rejected(self):
        (self.code.parent/'extra.c').write_text('int unexpected;\n')
        wrong = self.commit('unrecorded production input')
        with self.assertRaisesRegex(ValueError, 'outside sealed snapshot'):
            verify_source_commit(self.repo, wrong, self.sealed, self.archive)

if __name__ == '__main__':
    unittest.main()
