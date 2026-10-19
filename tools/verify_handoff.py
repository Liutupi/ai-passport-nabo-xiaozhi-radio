"""Verify the portable artwork, source fonts and exact firmware checkpoint."""
import hashlib
import json
from pathlib import Path

root = Path(__file__).resolve().parents[1]
manifest = json.loads((root / 'handoff/resources-2026-10-19.json').read_text('utf-8'))
total = 0
for entry in manifest['files']:
    path = (root / entry['path']).resolve()
    if not path.is_relative_to(root) or not path.is_file():
        raise SystemExit('Missing or unsafe resource: ' + entry['path'])
    data = path.read_bytes()
    if len(data) != entry['bytes'] or hashlib.sha256(data).hexdigest() != entry['sha256']:
        raise SystemExit('Resource mismatch: ' + entry['path'])
    total += len(data)
assert total == manifest['total_bytes']
assert manifest['source_design_images_all_copied']
print(f"PASS: {len(manifest['files'])} resource/checkpoint files; {total} bytes; "
      f"all {manifest['source_design_images']} original design images included")
