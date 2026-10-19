"""Build read-in-place radio catalog, 4-bit font subsets and small RGB565 props.

Run with Pillow and fontTools. Fonts stay external; output is reproducible and
contains only glyphs used by the catalog/UI. NFF1: header <4sHH>, then sorted
<IIhhhBB> glyph records (codepoint, bitmap offset, advance, left, top, w, h),
then packed 4-bit coverage, high nibble first. All offsets are file-relative.
"""
import argparse
import hashlib
import json
import struct
from pathlib import Path
from PIL import Image, ImageFont
from fontTools.ttLib import TTFont

ROOT = Path(__file__).resolve().parents[1]
BOARD = ROOT / 'xiaozhi-esp32/main/boards/folotoy/ai-passport-nabo'
TYPES = ['新闻', '音乐', '交通', '财经', '文艺', '生活', '体育', '方言', '综合', '戏曲', '其他']


def catalog(source):
    stations = json.loads(source.read_text('utf-8'))['stations']
    regions = list(dict.fromkeys(s['region'] for s in stations))
    strings, offsets = bytearray(b'\0'), {'': 0}
    def ref(s):
        if s not in offsets:
            offsets[s] = len(strings)
            strings.extend(s.encode('utf-8') + b'\0')
        return offsets[s]
    region_refs = [ref(s) for s in regions]
    records = bytearray()
    for s in stations:
        # Stable identity survives sorting, weekly catalog updates and NVS migration.
        uid = int.from_bytes(hashlib.sha256(s['url'].encode()).digest()[:4], 'little')
        qt = int(s.get('qt') or 0)
        mask = sum(1 << i for i, t in enumerate(TYPES) if t in s['types']) or 1 << 10
        records.extend(struct.pack('<IIIIIIIBBH', uid, ref(s['name']), ref('' if qt else s['url']),
                                   ref(s['py']), s['heat'], qt, ref(s['region']),
                                   regions.index(s['region']), {'mp3':0, 'aac':1, 'hls':2}.get(s['codec'], 0), mask))
    start = 12 + 4*len(regions) + len(records)
    return struct.pack('<4sHHI', b'NFC1', len(stations), len(regions), start) + struct.pack('<'+'I'*len(regions), *region_refs) + records + strings


def font(path, pixels, chars, out):
    ft = TTFont(path)
    cmap = ft.getBestCmap()
    missing = sorted(c for c in chars if ord(c) not in cmap)
    if missing:
        raise ValueError(f'{path.name}: missing glyphs {missing}')
    f = ImageFont.truetype(str(path), pixels)
    glyphs = []
    data = bytearray()
    offset = 8 + len(chars)*16
    for ch in sorted(chars):
        left, top, right, bottom = f.getbbox(ch, anchor='lt')
        # anchor ls gives a stable baseline across all characters.
        left, top, right, bottom = f.getbbox(ch, anchor='ls')
        w, h = right-left, bottom-top
        im = Image.new('L', (max(w, 1), max(h, 1)))
        if w and h:
            from PIL import ImageDraw
            ImageDraw.Draw(im).text((-left, -top), ch, font=f, fill=255, anchor='ls')
        coverage = [round(v*15/255) for v in im.getdata()] if w and h else []
        coverage += [0] * (len(coverage) % 2)
        bitmap = bytes((coverage[i] << 4) | coverage[i+1] for i in range(0, len(coverage), 2))
        glyphs.append(struct.pack('<IIhhhBB', ord(ch), offset + len(data), round(f.getlength(ch)), left, top, w, h))
        data.extend(bitmap)
    out.write_bytes(struct.pack('<4sHH', b'NFF1', len(chars), pixels) + b''.join(glyphs) + data)


def main():
    p = argparse.ArgumentParser(description=__doc__)
    p.add_argument('--design', type=Path, required=True)
    p.add_argument('--wenkai', type=Path, required=True)
    p.add_argument('--sans', type=Path, required=True)
    a = p.parse_args()
    dest = BOARD/'radio_assets'
    dest.mkdir(exist_ok=True)
    source = a.design/'catalog/stations.json'
    stations = json.loads(source.read_text('utf-8'))['stations']
    (dest/'radio_catalog.bin').write_bytes(catalog(source))
    (dest/'radio_sprites.bin').write_bytes((a.design/'art/sprites/radio_sprites.bin').read_bytes())
    ui = ''.join(f.read_text('utf-8') for f in BOARD.glob('nabo_radio*.[ch]'))
    ui += ''.join(f.read_text('utf-8') for f in BOARD.glob('nabo_radio*.cc'))
    ui += ''.join(s['name']+s['region']+''.join(s['types']) for s in stations)
    chars = set(chr(i) for i in range(32,127)) | {c for c in ui if 0x3000 <= ord(c) <= 0x9fff or 0xff00 <= ord(c) <= 0xffef}
    # Only text glyphs, no source comments' emoji. The same set supports future list/search screens.
    chars |= set('…♥♡▲▼·→')
    font(a.wenkai, 23, chars, dest/'radio_name.bin')
    font(a.sans, 12, chars, dest/'radio_ui.bin')
    font(a.wenkai, 14, set('NABO FM'), dest/'radio_logo.bin')
    font(a.sans, 8, set('ON AIR'), dest/'radio_led.bin')
    font(a.wenkai, 60, set('0123456789:'), dest/'radio_clock.bin')
    for name, size in [('mic',(26,36)), ('nabo_head_24',(24,24))]:
        im = Image.open(a.design/f'art/props/{name}.png').convert('RGBA').resize(size, Image.Resampling.LANCZOS)
        blob = bytearray(struct.pack('<4sHH', b'NFP1', *size))
        for r,g,b,alpha in im.getdata():
            value = ((r>>3)<<11) | ((g>>2)<<5) | (b>>3)
            blob += struct.pack('<HB', value, alpha)
        (dest/f'radio_{name}.bin').write_bytes(blob)
    report = {f.name: {'bytes':f.stat().st_size, 'sha256':hashlib.sha256(f.read_bytes()).hexdigest()}
              for f in sorted(dest.glob('*.bin'))}
    report['total_bytes'] = sum(f['bytes'] for f in report.values())
    report['font_sources'] = {role: {'file':path.name, 'sha256':hashlib.sha256(path.read_bytes()).hexdigest()}
                              for role,path in [('wenkai',a.wenkai),('sans',a.sans)]}
    report['font_glyphs'] = len(chars)
    (ROOT/'docs/radio-assets.json').write_text(json.dumps(report, indent=2)+'\n', 'utf-8')
    print(json.dumps(report, indent=2))

if __name__ == '__main__': main()
