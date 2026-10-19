from pathlib import Path
from PIL import Image,ImageDraw,ImageFont
import numpy as np,json,hashlib,colorsys,math
art=Path(__file__).resolve().parents[1]
props=art/'props';preview=art/'preview';(props/'game').mkdir(exist_ok=True)
def save(im,path):
    if path.exists():raise RuntimeError('Refusing overwrite: '+str(path))
    im.save(path)
def resize(im,size):return im.convert('RGBa').resize(size,Image.Resampling.LANCZOS).convert('RGBA')
def q64(im):
    a=im.getchannel('A')
    rgb=im.convert('RGB').quantize(colors=64,method=Image.Quantize.MEDIANCUT,dither=Image.Dither.NONE).convert('RGB')
    out=rgb.convert('RGBA');out.putalpha(a)
    arr=np.array(out);arr[:,:,3][arr[:,:,3]>=250]=255;arr[arr[:,:,3]<4]=0
    return Image.fromarray(arr)
def trim(im):
    a=np.array(im.getchannel('A'));bbox=Image.fromarray((a>=32).astype('uint8')*255).getbbox()
    if not bbox:raise RuntimeError('Empty generated source')
    l,t,r,b=bbox;bbox=(max(0,l-2),max(0,t-2),min(im.width,r+2),min(im.height,b+2))
    im=im.crop(bbox);arr=np.array(im);arr[arr[:,:,3]<4]=0
    return Image.fromarray(arr),bbox
def fit(im,size,pad=2):
    scale=min((size[0]-2*pad)/im.width,(size[1]-2*pad)/im.height)
    wh=(max(1,round(im.width*scale)),max(1,round(im.height*scale)))
    out=Image.new('RGBA',size);out.alpha_composite(resize(im,wh),((size[0]-wh[0])//2,(size[1]-wh[1])//2))
    return q64(out)
# Match the rendered coral material to the approved B reference, without changing geometry.
def coral_match(im):
    arr=np.array(im).astype(float);rgb=arr[:,:,:3]/255
    mx=rgb.max(2);mn=rgb.min(2);sat=np.divide(mx-mn,mx,out=np.zeros_like(mx),where=mx>0)
    mask=(arr[:,:,3]>128)&(rgb[:,:,0]>rgb[:,:,1]*1.15)&(rgb[:,:,1]>rgb[:,:,2]*1.02)&(sat>.30)
    b=np.array(Image.open(art/'design'/'phones_b.png').convert('RGBA')).astype(float)
    y,x=np.indices(b.shape[:2]);s=.827300930713547;tx=512-1254*s/2;ty=96-118*s
    sx=np.rint((x-tx)/s).astype(int);sy=np.rint((y-ty)/s).astype(int)
    sm=np.array(Image.open(art/'design'/'headphone_palette_mask.png').convert('RGBA'))[:,:,3]>0
    valid=(sx>=0)&(sx<sm.shape[1])&(sy>=0)&(sy<sm.shape[0]);bm=np.zeros(x.shape,bool);bm[valid]=sm[sy[valid],sx[valid]]
    target=np.median(b[:,:,:3][bm],axis=0)/255
    old=np.median(rgb[mask],axis=0)
    th,ts,tv=colorsys.rgb_to_hsv(*target);oh,os,ov=colorsys.rgb_to_hsv(*old)
    for yy,xx in zip(*np.where(mask)):
        h,s,v=colorsys.rgb_to_hsv(*rgb[yy,xx])
        arr[yy,xx,:3]=np.array(colorsys.hsv_to_rgb((h+(th-oh))%1,min(1,s*ts/os),min(1,v*tv/ov)))*255
    return Image.fromarray(np.clip(np.rint(arr),0,255).astype('uint8')),{'selected_B_median_hex':'#'+''.join(f'{round(v*255):02X}' for v in target),'generated_before_median_hex':'#'+''.join(f'{round(v*255):02X}' for v in old),'method':'HSV shell-only color calibration; no geometric change'}
specs={'mic':((104,144),(26,36)),'mug':((96,80),(24,20)),'headphones':((160,140),(40,35))}
records={};native={}
for name,(export,target) in specs.items():
    im,bbox=trim(Image.open(props/'source'/f'{name}_source.png').convert('RGBA'))
    calibration=None
    if name=='headphones':im,calibration=coral_match(im)
    master=fit(im,export,2);save(master,props/f'{name}.png')
    game=q64(resize(master,target));save(game,props/'game'/f'{name}.png');native[name]=game
    records[name]={'file':f'props/{name}.png','export_px':list(export),'target_px':list(target),'export_scale':4,'native_file':f'props/game/{name}.png','mode':'RGBA','source':f'props/source/{name}_source.png','source_crop':list(bbox),'alpha_bbox':list(master.getchannel('A').getbbox()),'color_calibration':calibration}
head=Image.open(art/'design'/'S2_on_air_cutout.png').convert('RGBA').crop((150,55,872,666))
head96=fit(head,(96,96),2);head24=q64(resize(head96,(24,24)))
save(head96,props/'nabo_head_96.png');save(head24,props/'nabo_head_24.png')
save(head24.resize((96,96),Image.Resampling.NEAREST),props/'nabo_head_24_4x_nearest.png')
native['nabo_head']=head24
records['nabo_head_24']={'file':'props/nabo_head_24.png','export_px':[24,24],'target_px':[24,24],'export_scale':1,'hi_res_file':'props/nabo_head_96.png','hi_res_px':[96,96],'hi_res_scale':4,'nearest_check_file':'props/nabo_head_24_4x_nearest.png','mode':'RGBA','source':'design/S2_on_air_cutout.png','source_crop':[150,55,872,666],'identity':'crop of approved S2; no redraw'}
base={'bg':'#0B0F1A','stage':'#161C2B','desk':'#262C3A','dial_bg':'#1C140A','dial':'#FFB547','dial_dim':'#784E18','on_air':'#FF3B30','level':'#4FD1C5','text':'#F2EDE4','muted':'#7C8597','headphones_B':'#E97967','ear_cushion':'#2B2930','video_desk':'#2A2230'}
def p565(v):
    r,g,b=tuple(bytes.fromhex(v[1:]));return ((r>>3)<<11)|((g>>2)<<5)|(b>>3)
palette={'schema_version':1,'format':'RGB565','conversion':'((R >> 3) << 11) | ((G >> 2) << 5) | (B >> 3)','colors':{k:{'hex':v,'rgb565':p565(v),'rgb565_hex':f'0x{p565(v):04X}'} for k,v in base.items()},'changes_from_ART_BRIEF':'Original ten colors unchanged. Added selected B coral, ear cushion and video-desk roles; no starting colors adjusted.'}
(art/'palette'/'radio.json').write_text(json.dumps(palette,ensure_ascii=False,indent=2)+'\n',encoding='utf-8')
# Static UI composition follows project layout; all controls here are preview drawing only.
im=Image.new('RGBA',(240,320),base['bg']);d=ImageDraw.Draw(im)
fontpath=r'C:\Windows\Fonts\msyh.ttc';mono=r'C:\Windows\Fonts\consolab.ttf'
def font(n,latin=False):return ImageFont.truetype(mono if latin else fontpath,n)
def text(xy,s,n=10,fill='text',latin=False):
    d.text(xy,s,font=font(n,latin),fill=base.get(fill,fill))
d.rectangle((0,18,239,149),fill=base['stage'])
for x in (24,48,72,96,144,168,192,216):d.line((x,18,x,149),fill='#1B2333')
# Character uses same shared source transform. Desk anchor is exactly screen y150.
hero=Image.open(art/'design'/'S2_on_air_cutout.png').convert('RGBA');scaled=resize(hero,(164,164))
hero_pos=(38,7);im.alpha_composite(scaled,hero_pos)
d=ImageDraw.Draw(im);d.rectangle((0,0,239,15),fill=base['bg'])
text((5,1),'NABO FM',11,'dial',True)
d.rounded_rectangle((150,3,188,15),radius=3,fill=base['on_air']);d.text((154,3),'ON AIR',font=font(8,True),fill='#FFFFFF')
text((194,2),'30m',9,'muted',True)
im.alpha_composite(head24,(5,22))
# Program-rendered boom stays behind generated microphone.
d=ImageDraw.Draw(im);d.line((229,40,198,92),fill='#596172',width=2)
mic_pos=(184,92);im.alpha_composite(native['mic'],mic_pos)
d=ImageDraw.Draw(im);d.rectangle((0,150,239,177),fill=base['desk']);d.line((0,150,239,150),fill='#4B4054')
heights=[2,4,7,4,3,6,8,5,3,2,4,1]
for i,h in enumerate(heights):
    for j in range(h):
        color=base['dial'] if j>=6 else base['level'];x=14+i*7;y=172-j*2;d.rectangle((x,y,x+4,y+1),fill=color)
d.ellipse((202,156,218,172),outline=base['dial'],width=2);d.line((210,164,214,158),fill=base['dial'],width=2)
title='广东音乐之声';box=d.textbbox((0,0),title,font=font(22));text(((240-(box[2]-box[0]))//2,179),title,22)
status='广东电台 · 播放中';text((53,208),status,10,'muted')
hx=round(53+d.textlength(status,font=font(10))+5);hy=214
d.ellipse((hx,hy,hx+3,hy+3),fill=base['muted']);d.ellipse((hx+3,hy,hx+6,hy+3),fill=base['muted']);d.polygon([(hx,hy+2),(hx+6,hy+2),(hx+3,hy+6)],fill=base['muted'])
d.rounded_rectangle((8,226,231,281),radius=6,fill=base['dial_bg'],outline=base['dial_dim'])
for x in range(15,230,6):
    high=13 if ((x-15)//6)%5==0 else 7;d.line((x,247,x,247-high),fill=base['dial'] if high==13 else base['dial_dim'])
for x,label in [(30,'42'),(75,'43'),(120,'44'),(165,'45'),(210,'46')]:
    d.rectangle((x-2,254,x+2,258),fill=base['dial']);d.text((x-5,263),label,font=font(9,True),fill=base['dial'])
d.line((120,230,120,278),fill=base['on_air'],width=2)
text((22,287),'▲▼ 调台   OK 播放/暂停',11,'muted')
text((31,305),'双击▲返回  长按OK收藏',10,'muted')
save(im,preview/'props_radio.png');save(im.resize((480,640),Image.Resampling.NEAREST),preview/'props_radio_2x.png')
a=np.array(im);q=a.copy()
for channel,bits in ((0,5),(1,6),(2,5)):
    v=(a[:,:,channel]>>(8-bits));q[:,:,channel]=((v.astype('uint16')*255)//((1<<bits)-1)).astype('uint8')
save(Image.fromarray(q),preview/'props_radio_rgb565.png')
sheet=Image.new('RGBA',(1440,760),base['bg']);sd=ImageDraw.Draw(sheet)
names=['mic','mug','headphones','nabo_head'];labels=['MIC / 26 x 36','MUG / 24 x 20','HEADPHONES B / 40 x 35','NABO HEAD / 24 x 24']
for i,name in enumerate(names):
    x=i*360;sd.text((x+20,26),labels[i],font=font(18,True),fill=base['text'])
    sd.text((x+20,70),'4x RGBA export',font=font(14,True),fill=base['muted'])
    master=Image.open(props/(name+'.png' if name!='nabo_head' else 'nabo_head_96.png')).convert('RGBA')
    tile=Image.new('RGBA',(320,180),base['stage']);tile.alpha_composite(master,((320-master.width)//2,(180-master.height)//2));sheet.alpha_composite(tile,(x+20,100))
    sd.text((x+20,300),'Native game size',font=font(14,True),fill=base['muted']);game=native[name]
    sheet.alpha_composite(game,(x+(360-game.width)//2,335))
    sd.text((x+20,390),'8x nearest check',font=font(14,True),fill=base['muted'])
    check=game.resize((game.width*8,game.height*8),Image.Resampling.NEAREST)
    tile=Image.new('RGBA',(320,300),base['stage']);tile.alpha_composite(check,((320-check.width)//2,(300-check.height)//2));sheet.alpha_composite(tile,(x+20,430))
save(sheet,preview/'props_sheet.png')
actions=[('idle_off',8,True,'required','S1'),('phones_on',6,False,'required','S1->S2'),('phones_off',6,False,'required','S2->S1'),('tune',6,True,'required','S2'),('groove',8,True,'required','S2'),('listen',8,True,'required','S2'),('sip',8,False,'required','S2'),('love',5,False,'required','S2'),('oops',5,False,'required','S2'),('doze',8,True,'required','S2'),('wave',6,False,'recommended','S1'),('search',6,True,'recommended','S1'),('sing',8,True,'optional','S2')]
manifest={'schema_version':1,'project':'NABO FM','selected_headphones':'B coral','screen_px':[240,320],'props':records,'palette_file':'palette/radio.json','previews':['preview/props_radio.png','preview/props_radio_2x.png','preview/props_radio_rgb565.png','preview/props_sheet.png'],'sources':{'S1':{'file':'design/S1_off_air.png','sha256':hashlib.sha256((art/'design'/'S1_off_air.png').read_bytes()).hexdigest()},'S2':{'file':'design/S2_on_air.png','sha256':hashlib.sha256((art/'design'/'S2_on_air.png').read_bytes()).hexdigest()}},'preview_layout':{'hero_image_px':[164,164],'hero_origin':list(hero_pos),'hero_hat_to_desk_nominal_px':128,'desk_y':150,'mic_rect':[184,92,26,36],'avatar_rect':[5,22,24,24],'character_face_rect':[89,76,61,33],'controls_y':[150,178],'station_y':[178,222],'dial_y':[222,282],'hints_y':[282,320]},'animation_spec':{'fps':12,'frame_px':[256,256],'anchor':[128,224],'palette_colors_per_action':64,'required_actions':10,'required_target_frames':68,'all_actions':13,'all_target_frames':88,'status':'not_produced_no_source_video','sip_mug_reference':'props/mug.png','same_first_pose':['S2','listen frame0','groove frame0'],'actions':[{'name':n,'target_frames':f,'loop':l,'priority':r,'start':s,'status':'awaiting_source_video'} for n,f,l,r,s in actions]}}
(art/'manifest.json').write_text(json.dumps(manifest,ensure_ascii=False,indent=2)+'\n',encoding='utf-8')
(art/'delivery'/'round3_export_summary.json').write_text(json.dumps({'props':records,'manifest':'manifest.json','preview':'preview/props_radio.png'},ensure_ascii=False,indent=2)+'\n',encoding='utf-8')
print(json.dumps({'exported_props':{k:v['export_px'] for k,v in records.items()},'previews':manifest['previews'],'animations_produced':0},ensure_ascii=False))

