from pathlib import Path
from PIL import Image
import numpy as np,json,hashlib
art=Path(__file__).resolve().parents[1]
m=json.loads((art/'manifest.json').read_text(encoding='utf-8'))
report={'status':'passed','scope':'Round 3 static art, props, palette, previews; no video/animation validation','props':{}}
for key,spec in m['props'].items():
    p=art/spec['file'];im=Image.open(p);a=np.array(im.convert('RGBA')).astype(int)
    assert im.mode=='RGBA' and list(im.size)==spec['export_px']
    assert (a[:,:,3]==0).any() and (a[:,:,3]==255).any()
    border=np.concatenate([a[0,:,3],a[-1,:,3],a[:,0,3],a[:,-1,3]])
    assert (a[[0,0,-1,-1],[0,-1,0,-1],3]==0).all(),'nontransparent corners'
    assert int(border.max())<255,'opaque silhouette clipped at canvas edge'
    if key!='nabo_head_24':assert int(border.max())==0
    fg=a[:,:,3]>0;green=fg&(a[:,:,1]>80)&(a[:,:,1]>a[:,:,0]*1.25+15)&(a[:,:,1]>a[:,:,2]*1.25+15)
    assert int(green.sum())==0
    colors=len(np.unique(a[:,:,:3][fg],axis=0))
    assert colors<=64
    target=spec['target_px']
    if key=='nabo_head_24':
        assert Image.open(art/spec['hi_res_file']).size==(96,96)
    else:
        assert spec['export_px']==[x*4 for x in target]
        assert Image.open(art/spec['native_file']).size==tuple(target)
    report['props'][key]={'file':spec['file'],'export_px':spec['export_px'],'target_px':target,'alpha_zero':int((a[:,:,3]==0).sum()),'alpha_partial':int(((a[:,:,3]>0)&(a[:,:,3]<255)).sum()),'visible_rgb_colors':colors,'green_foreground_pixels':0,'clear_outer_border':bool(border.max()==0),'outer_edge_max_alpha':int(border.max()),'no_opaque_clipping':True,'alpha_bbox':list(im.getchannel('A').getbbox()),'sha256':hashlib.sha256(p.read_bytes()).hexdigest()}
pal=json.loads((art/'palette'/'radio.json').read_text(encoding='utf-8'))
for key,c in pal['colors'].items():
    r,g,b=bytes.fromhex(c['hex'][1:]);v=((r>>3)<<11)|((g>>2)<<5)|(b>>3)
    assert v==c['rgb565'] and c['rgb565_hex']==f'0x{v:04X}'
base={'bg':'#0B0F1A','stage':'#161C2B','desk':'#262C3A','dial_bg':'#1C140A','dial':'#FFB547','dial_dim':'#784E18','on_air':'#FF3B30','level':'#4FD1C5','text':'#F2EDE4','muted':'#7C8597'}
assert all(pal['colors'][k]['hex']==v for k,v in base.items())
radio=np.array(Image.open(art/'preview'/'props_radio.png').convert('RGBA'))
double=np.array(Image.open(art/'preview'/'props_radio_2x.png').convert('RGBA'))
assert radio.shape==(320,240,4) and (radio[:,:,3]==255).all()
assert np.array_equal(double,np.repeat(np.repeat(radio,2,0),2,1))
assert Image.open(art/'preview'/'props_radio_rgb565.png').size==(240,320)
assert Image.open(art/'preview'/'props_sheet.png').size==(1440,760)
face=m['preview_layout']['character_face_rect'];mic=m['preview_layout']['mic_rect']
overlap=max(0,min(face[0]+face[2],mic[0]+mic[2])-max(face[0],mic[0]))*max(0,min(face[1]+face[3],mic[1]+mic[3])-max(face[1],mic[1]))
assert overlap==0
expected={'phones_b.png':'5032cb43ae26682c14b8bbd86f2f0a7e1bb7555e95785d66bdf484ce4af2edb0','S1_off_air.png':'a7219b908d6885924f19912b56fd1e5d8ccd3f0a888ee06e02851747041fae3a','S2_on_air.png':'5032cb43ae26682c14b8bbd86f2f0a7e1bb7555e95785d66bdf484ce4af2edb0'}
assert all(hashlib.sha256((art/'design'/k).read_bytes()).hexdigest()==v for k,v in expected.items())
anim=m['animation_spec'];assert sum(a['target_frames'] for a in anim['actions'])==88
assert sum(a['target_frames'] for a in anim['actions'] if a['priority']=='required')==68
assert all(a['status']=='awaiting_source_video' for a in anim['actions'])
report['checks']={'manifest_paths_exist':True,'palette_RGB565_all_roles_exact':True,'original_ten_palette_colors_unchanged':True,'preview_px':[240,320],'preview_2x_nearest_exact':True,'mic_face_overlap_pixels':0,'mic_face_horizontal_gap_px':mic[0]-(face[0]+face[2]),'B_S1_S2_hashes_unchanged':True,'head_icon_source':'approved S2 crop; no redraw','headphones_geometry':'visual QA: one band, two upward-facing cushion openings, both links natural','mic':'visual QA: dark mesh + amber ring, no stand or letters','mug':'visual QA: warm plain body, open right handle, no text/steam/saucer','visible_white_sticker_border':'none added','mirroring':'no source or prop mirrored','animations_generated':0,'source_video_count':len(list((art/'video').rglob('*.mp4'))) if (art/'video').exists() else 0,'animation_loops_hands_fps':'not tested; no source video'}
(art/'delivery'/'round3_validation.json').write_text(json.dumps(report,ensure_ascii=False,indent=2)+'\n',encoding='utf-8')
print(json.dumps(report,ensure_ascii=False))

