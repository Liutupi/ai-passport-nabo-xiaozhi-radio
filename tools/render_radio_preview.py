"""Convert test_radio_view.py's exact production RGB565 frames to review PNGs."""
from pathlib import Path
import struct
from PIL import Image, ImageDraw
ROOT=Path(__file__).resolve().parents[1]
folder=ROOT/'private/fm-preview'
dest=ROOT/'docs/images/radio'
dest.mkdir(parents=True,exist_ok=True)
names=['player','playing','favorite','menu','list','favorites-list','search','search-a','clock','awakened']
sheet=Image.new('RGB',(5*256,2*356),'#111725')
draw=ImageDraw.Draw(sheet)
for i,name in enumerate(names):
    values=struct.unpack('<76800H',(folder/(name+'.rgb565')).read_bytes())
    im=Image.new('RGB',(240,320))
    im.putdata([(((c>>11)&31)*255//31,((c>>5)&63)*255//63,(c&31)*255//31) for c in values])
    im.save(dest/(name+'.png'))
    x=(i%5)*256+8;y=(i//5)*356+24
    sheet.paste(im,(x,y));draw.text((x,y-17),name,fill='#f7b955')
sheet.save(dest/'contact-sheet.png')
print(dest/'contact-sheet.png')
