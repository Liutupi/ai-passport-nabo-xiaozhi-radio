"""Host-only regression suite; never opens a device or writes flash."""
import subprocess
import sys
from pathlib import Path

root = Path(__file__).resolve().parents[1]
for script in ['test_release_package.py', 'run_host_tests.py', 'test_voice_commands.py', 'test_stereo_chunks.py',
               'test_radio_chunking.py', 'test_radio_dsp.py', 'test_helix_allocator.py', 'test_ported_apps.py']:
    print('Running', script, flush=True)
    subprocess.run([sys.executable, str(root / script)], cwd=root, check=True)
