"""Bind a release commit to a sealed firmware's production-source snapshot."""
import hashlib
from pathlib import Path, PurePosixPath
import re
import subprocess


def production_input(relative):
    path = PurePosixPath(relative)
    if relative.startswith('configs/'):
        return True
    if not relative.startswith('xiaozhi-esp32/'):
        return False
    return (path.suffix.lower() in {'.c', '.cc', '.cpp', '.h', '.hpp', '.s', '.ld',
                                   '.bin', '.csv', '.json', '.yml', '.yaml'} or
            path.name == 'CMakeLists.txt' or path.name == 'dependencies.lock' or
            path.name.startswith(('sdkconfig', 'Kconfig')))


def verify_source_commit(repository, commit, sealed, archive):
    """Read Git blobs only; never modifies the repository or sealed archive.

    Git can store LF while the Windows build snapshot has CRLF. Text is
    compared after CRLF normalization; binary asset inputs stay byte-exact.
    """
    repository, archive = Path(repository).resolve(), Path(archive).resolve()
    base = sealed['source_base_commit']
    if not re.fullmatch(r'[0-9a-fA-F]{40}', commit) or not re.fullmatch(r'[0-9a-fA-F]{40}', base):
        raise ValueError('Release and base source commits must be full commit SHAs')
    if sealed.get('source_working_tree_dirty') and commit.lower() == base.lower():
        raise ValueError('Dirty sealed candidate cannot use its base commit as full release source')

    def git(*args):
        result = subprocess.run(['git', *args], cwd=repository, capture_output=True)
        if result.returncode:
            raise ValueError('Cannot verify release production source in Git')
        return result.stdout

    if git('cat-file', '-t', commit).strip() != b'commit' or git('cat-file', '-t', base).strip() != b'commit':
        raise ValueError('Release/base identity must reference existing commits')
    expected = {name: entry for name, entry in sealed['source_files'].items()
                if production_input(name)}
    if not expected:
        raise ValueError('Sealed candidate lacks production-source evidence')
    changed = git('diff', '--name-only', '--no-renames', '-z', base, commit, '--',
                  'xiaozhi-esp32', 'configs').decode('utf-8').split('\0')
    for name in filter(None, changed):
        if production_input(name) and name not in expected:
            raise ValueError('Release commit changes production input outside sealed snapshot: '+name)
    for name, entry in expected.items():
        path = (archive/'source-state'/name).resolve()
        if not path.is_relative_to(archive/'source-state'):
            raise ValueError('Unsafe sealed source path')
        snapshot = path.read_bytes()
        if len(snapshot) != entry['bytes'] or hashlib.sha256(snapshot).hexdigest() != entry['sha256']:
            raise ValueError('Sealed production-source snapshot changed: '+name)
        committed = git('show', commit+':'+name)
        if PurePosixPath(name).suffix.lower() == '.bin':
            equal = snapshot == committed
        else:
            equal = snapshot.replace(b'\r\n', b'\n') == committed.replace(b'\r\n', b'\n')
        if not equal:
            raise ValueError('Release commit differs from sealed production source: '+name)
    return {'status':'PASS', 'production_files':len(expected),
            'comparison':'Git blobs; text CRLF normalized to LF, binary inputs byte-exact',
            'unexpected_production_changes':False}
