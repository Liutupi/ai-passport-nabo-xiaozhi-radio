"""Create exact-copy SVG community posters from the existing NABO artwork and UI.

SVG is the editable source; text is outlined for portable Chinese font rendering.
Export the SVG files with an SVG-capable rasterizer at their native 900 x 1200 size.
No device photo or invented firmware screen is used.
"""
import base64
import hashlib
import html
import json
from pathlib import Path
from fontTools.ttLib import TTFont
from fontTools.pens.svgPathPen import SVGPathPen

ROOT = Path(__file__).resolve().parents[1]
OUT = ROOT / 'docs/images/community-v1.0.0'
OUT.mkdir(parents=True, exist_ok=True)
NAVY, AMBER, CORAL, CREAM, MUTED = '#10141f', '#ffbe53', '#ff7054', '#f5f0e7', '#a2a7b3'
FONT_FILES = {
    'sans': ROOT / 'design/nabo-radio/fonts/SourceHanSansSC-Normal.otf',
    'title': ROOT / 'design/nabo-radio/fonts/LXGWWenKai-Regular.ttf',
    'bold': ROOT / 'design/nabo-radio/prototype/fonts/sans_bold.woff2',
}
FONTS = {k: TTFont(v) for k, v in FONT_FILES.items()}
SOURCES = {}


def text(value, x, y, size, color=CREAM, font='sans', max_width=None, align='left', spacing=0):
    f = FONTS[font]
    glyphs, cmap = f.getGlyphSet(), f.getBestCmap()
    units = f['head'].unitsPerEm
    names = [cmap.get(ord(c)) for c in value]
    assert all(names), 'Missing glyph: ' + value
    advance = sum(f['hmtx'][n][0] for n in names)
    width = advance * size / units + spacing * max(0, len(value)-1)
    if max_width and width > max_width:
        return text(value, x, y, size * max_width / width, color, font, None, align, spacing)
    cursor = x - (width/2 if align == 'center' else width if align == 'right' else 0)
    items = [f'<g fill="{color}" aria-label="{html.escape(value, quote=True)}">']
    for n in names:
        pen = SVGPathPen(glyphs)
        glyphs[n].draw(pen)
        items.append(f'<path d="{pen.getCommands()}" transform="translate({cursor:.3f} {y}) scale({size/units:.6f} {-size/units:.6f})"/>')
        cursor += f['hmtx'][n][0] * size / units + spacing
    return ''.join(items) + '</g>'


def rect(x, y, w, h, fill, rx=0, stroke=None):
    return f'<rect x="{x}" y="{y}" width="{w}" height="{h}" rx="{rx}" fill="{fill}"' + (f' stroke="{stroke}"' if stroke else '') + '/>'


def line(x1, y1, x2, y2, color, width=1, opacity=1):
    return f'<path d="M{x1} {y1}H{x2}" stroke="{color}" stroke-width="{width}" opacity="{opacity}"/>' if y1 == y2 else f'<path d="M{x1} {y1}L{x2} {y2}" stroke="{color}" stroke-width="{width}" opacity="{opacity}"/>'


def image(relative, x, y, w, h):
    p = ROOT / relative
    data = p.read_bytes()
    SOURCES[relative] = hashlib.sha256(data).hexdigest()
    encoded = base64.b64encode(data).decode()
    return f'<image x="{x}" y="{y}" width="{w}" height="{h}" href="data:image/png;base64,{encoded}"/>'


def heart(x, y, scale=1, color=CORAL):
    return f'<path transform="translate({x} {y}) scale({scale})" fill="{color}" d="M0 20C-30-2-67-25-67-63C-67-101-22-112 0-78C22-112 67-101 67-63C67-25 30-2 0 20Z"/>'


def base(index, section, light=False):
    ink, bg = (NAVY, CREAM) if light else (CREAM, NAVY)
    parts = ['<svg xmlns="http://www.w3.org/2000/svg" xmlns:xlink="http://www.w3.org/1999/xlink" width="900" height="1200" viewBox="0 0 900 1200">', rect(0, 0, 900, 1200, bg)]
    parts += [text('NABO FM', 58, 64, 24, ink, 'bold'), text(section, 842, 64, 17, ink, 'sans', align='right', spacing=1), line(58, 91, 842, 91, ink, opacity=.22)]
    parts += [line(58, 1114, 842, 1114, ink, opacity=.22), text('AI PASSPORT / v1.0.0 首版体验', 58, 1155, 18, ink), text(f'0{index} / 05', 842, 1155, 18, ink, align='right')]
    return parts


def screen(name, x, y, w):
    return image(f'docs/images/radio/{name}.png', x, y, w, w*4/3)


def finish(filename, parts):
    p = OUT / (filename + '.svg')
    p.write_text(''.join(parts) + '</svg>', 'utf-8')
    return p.name


files = []
# 01 / Hero: a clear title, the original large character, and a strong radio dial.
p = base(1, 'ON AIR / 工牌收音机')
p += [text('NABO FM', 48, 225, 137, AMBER, 'bold', max_width=804), text('把喜欢的声音，', 60, 320, 57, font='title'), text('装进工牌。', 60, 391, 57, font='title')]
p += ['<g fill="none" stroke="#ffbe53" opacity=".22"><circle cx="450" cy="775" r="335"/><circle cx="450" cy="775" r="356"/></g>']
for n in range(33):
    import math
    angle = math.radians(195+n*8.4)
    x1, y1 = 450+335*math.cos(angle), 775+335*math.sin(angle)
    x2, y2 = 450+(348 if n % 4 else 360)*math.cos(angle), 775+(348 if n % 4 else 360)*math.sin(angle)
    p += [line(x1, y1, x2, y2, AMBER, 2, .7)]
p += [image('design/nabo-radio/art/design/S2_on_air_cutout.png', 25, 359, 855, 855), rect(0, 1031, 900, 80, NAVY), text('听电台   /   存收藏   /   看时间', 450, 1072, 29, AMBER, 'title', align='center'), text('NABO 原画与界面展示 · 非实机照片', 60, 430, 17, MUTED)]
files += [finish('01-cover', p)]
# 02 / Discovery: the number is explicitly a catalog size, never a playback claim.
p = base(2, 'LISTEN / 电台时光')
p += [text('今天，', 58, 182, 55, font='title'), text('想听哪一种声音？', 58, 251, 55, font='title', max_width=790), text('1,105', 53, 406, 133, AMBER, 'bold', max_width=508), text('台内置目录', 550, 388, 31, AMBER)]
p += [rect(58, 450, 384, 536, '#080b12', 7, '#4e4641'), screen('playing', 70, 463, 360), text('音乐。', 494, 550, 41, font='title'), text('新闻。', 494, 611, 41, font='title'), text('熟悉的地方台。', 494, 674, 37, font='title', max_width=340), line(495, 716, 839, 716, '#5c554b'), text('上下键换台', 495, 773, 27, AMBER), text('拼音首字母搜台', 495, 823, 27, AMBER), text('把日常的一小段', 495, 914, 25, MUTED), text('留给喜欢的声音。', 495, 956, 25, MUTED)]
p += [text('目录数量 ≠ 全部台可播；实际可用性依台源与网络。', 58, 1043, 21, MUTED, max_width=784), text('画面为生产代码宿主预览，非实机照片。', 58, 1079, 17, MUTED)]
files += [finish('02-listen', p)]
# 03 / Favorites: warm paper, a recognizable heart, and the real separate list.
p = base(3, 'FAVORITES / 喜欢就留下', True)
p += [text('喜欢的台，', 58, 185, 58, NAVY, 'title'), text('留在自己的列表。', 58, 258, 58, NAVY, 'title', max_width=790), heart(190, 549, 1.65), text('双击中键', 58, 713, 38, NAVY, 'title'), text('收藏 / 取消', 60, 766, 27, NAVY), text('长按中键打开菜单', 60, 856, 24, NAVY), text('进入「收藏列表」', 60, 900, 24, NAVY), rect(412, 355, 430, 592, NAVY, 8), screen('favorites-direct', 424, 368, 406), text('界面预览', 412, 985, 17, NAVY)]
p += [line(58, 1012, 842, 1012, NAVY, opacity=.2), text('下次想听，直接找到。', 58, 1073, 37, NAVY, 'title')]
files += [finish('03-favorites', p)]
# 04 / Time: the real UI makes the clock claim inspectable.
p = base(4, 'AFTER HOURS / 电台与时间')
p += [text('听着电台，', 58, 185, 57, font='title'), text('也别忘了时间。', 58, 257, 57, font='title'), rect(58, 342, 450, 620, '#080b12', 6, '#4e4641'), screen('clock', 70, 355, 426), text('北京时间', 556, 396, 29, AMBER), text('待机时钟', 556, 442, 29, AMBER), line(556, 480, 842, 480, '#4e4641')]
for label, sub, y in [('1 / 3', '分钟', 563), ('5 / 10', '分钟', 679), ('OFF', '关闭待机', 795)]:
    p += [text(label, 556, y, 48, CREAM, 'bold', max_width=278), text(sub, 558, y+39, 22, MUTED)]
p += [text('按自己的节奏来。', 556, 936, 25, AMBER, 'title', max_width=280), text('闲置多久进入时钟，由你选择。', 58, 1036, 28, font='title'), text('关闭待机后保留播放页；画面为宿主预览，非实机照片。', 58, 1080, 17, MUTED, max_width=784)]
files += [finish('04-clock', p)]
# 05 / Start: concise physical controls and honest first-install requirements.
p = base(5, 'GET STARTED / 简单开始', True)
p += [text('一块小工牌，', 58, 186, 57, NAVY, 'title'), text('多一点声音陪伴。', 58, 259, 57, NAVY, 'title', max_width=790)]
p += [image('design/nabo-radio/art/design/S2_on_air_cutout.png', -120, 304, 700, 700)]
for n, title, sub, y in [(1, '连上 Wi-Fi', '使用 2.4 GHz 网络', 420), (2, '打开网络电台', '主页下键 → 工具', 572), (3, '按下中键', '开始收听 / 暂停', 724)]:
    p += [text(f'0{n}', 504, y, 20, '#937b55', 'bold'), text(title, 504, y+48, 33, NAVY, 'title', max_width=335), text(sub, 504, y+88, 22, NAVY, max_width=335)]
p += [rect(58, 940, 784, 80, NAVY, 4), text('上下换台  ·  双击中键收藏  ·  长按中键菜单', 450, 989, 25, CREAM, 'title', align='center', max_width=744), text('首次完整刷机会清空配网与设置，刷后按屏幕提示重新配置。', 58, 1061, 18, NAVY, max_width=784), text('首版体验 · MP3 为主；部分台源、AAC 与长播仍待完善。', 58, 1091, 18, NAVY, max_width=784)]
files += [finish('05-start', p)]
manifest = {'version':'1.0.0','purpose':'AI Passport community launch gallery','dimensions':[900,1200],'format':'SVG source + PNG publication exports','files':files,'source_images':SOURCES,'font_sources':{k:str(v.relative_to(ROOT)) for k,v in FONT_FILES.items()},'provenance':'Deterministic vector layout and outlined exact copy using existing NABO artwork and production-code host UI previews. No device photos; no invented UI; development/design assisted by AI.','firmware_change':False}
(OUT/'manifest.json').write_text(json.dumps(manifest,ensure_ascii=False,indent=2)+'\n')
print('Created five 900 x 1200 SVG source posters:',OUT)
