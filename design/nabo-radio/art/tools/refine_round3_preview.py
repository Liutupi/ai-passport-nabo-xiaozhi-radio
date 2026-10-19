from pathlib import Path
from PIL import Image,ImageDraw,ImageFont
import numpy as np
import json,shutil
art=Path(__file__).resolve().parents[1];props=art/'props';preview=art/'preview'
script=art/'tools'/'export_round3.py';src=script.read_text(encoding='utf-8-sig')
old="text((53,208),'广东电台 · 播放中 · ♥',10,'muted')"
new="""status='广东电台 · 播放中';text((53,208),status,10,'muted')
hx=round(53+d.textlength(status,font=font(10))+5);hy=214
d.ellipse((hx,hy,hx+3,hy+3),fill=base['muted']);d.ellipse((hx+3,hy,hx+6,hy+3),fill=base['muted']);d.polygon([(hx,hy+2),(hx+6,hy+2),(hx+3,hy+6)],fill=base['muted'])"""
assert old in src
src=src.replace(old,new);script.write_text(src,encoding='utf-8')
archive=preview/'review_v1';archive.mkdir(exist_ok=False)
for name in ['props_radio.png','props_radio_2x.png','props_radio_rgb565.png']:
    shutil.move(str(preview/name),str(archive/name))
base={k:v['hex'] for k,v in json.loads((art/'palette'/'radio.json').read_text(encoding='utf-8'))['colors'].items()}
native={k:Image.open(props/'game'/f'{k}.png').convert('RGBA') for k in ['mic','mug','headphones']}
head24=Image.open(props/'nabo_head_24.png').convert('RGBA')
def resize(im,size):return im.convert('RGBa').resize(size,Image.Resampling.LANCZOS).convert('RGBA')
def save(im,path):
    if path.exists():raise RuntimeError('Refusing overwrite')
    im.save(path)
section=src[src.index('# Static UI composition'):src.index("sheet=Image.new")]
exec(compile(section,str(script),'exec'),globals())
print('Three radio previews regenerated; missing-heart glyph replaced with program-drawn UI heart.')

