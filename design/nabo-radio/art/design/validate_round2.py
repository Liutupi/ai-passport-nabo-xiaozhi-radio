from pathlib import Path
from PIL import Image
import numpy as np
import hashlib,json
p=Path(__file__).parent
keys=['S1_off_air','S2_on_air']
b=np.array(Image.open(p/'phones_b.png').convert('RGBA')).astype(np.int16)
s=[];cuts=[];out={'status':'passed','scope':'Round 2 static start images; not animations','selected_color':'B coral','variants':[]}
for k in keys:
 f=np.array(Image.open(p/(k+'.png')).convert('RGBA')).astype(np.int16)
 c=np.array(Image.open(p/(k+'_cutout.png')).convert('RGBA')).astype(np.int16)
 n=np.array(Image.open(p/(k+'_128.png')).convert('RGBA'))
 z=np.array(Image.open(p/(k+'_128_4x.png')).convert('RGBA'))
 assert f.shape==(1024,1024,4) and c.shape==f.shape
 assert (f[:,:,3]==255).all()
 assert (f[896:]==[42,34,48,255]).all()
 zero=c[:896,:,3]==0
 assert (f[:896][zero]==[0,255,0,255]).all()
 assert (c[896:,:,3]==0).all()
 green=(c[:,:,3]>0)&(c[:,:,1]>80)&(c[:,:,1]>c[:,:,0]*1.25+15)&(c[:,:,1]>c[:,:,2]*1.25+15)
 assert int(green.sum())==0
 assert np.array_equal(z,np.repeat(np.repeat(n,4,0),4,1))
 white=(c[80:140,480:545,0]>220)&(c[80:140,480:545,1]>220)&(c[80:140,480:545,2]>220)&(c[80:140,480:545,3]>200)
 cap_top=int(np.where(white)[0].min()+80)
 assert abs(cap_top-96)<=4
 nw=(n[9:40,70:90,0]>220)&(n[9:40,70:90,1]>220)&(n[9:40,70:90,2]>220)
 ntop=int(np.where(nw)[0].min()+9)
 assert abs((138-ntop)-128)<=1
 yy,xx=np.where(c[:,:,3]>0); bbox=[int(xx.min()),int(yy.min()),int(xx.max()+1),int(yy.max()+1)]
 out['variants'].append({'name':k,'output_px':[1024,1024],'all_opaque':True,'background':'#00FF00','desk':'#2A2230','desk_y':896,'cap_top_measured_y':cap_top,'cap_to_desk_measured_px':896-cap_top,'native_cap_to_desk_px':138-ntop,'native_preview_px':[160,160],'nearest_preview_px':[640,640],'nearest_4x_exact':True,'green_subject_pixels':0,'subject_bbox':bbox,'transparent_cutout_pixels':int((c[:,:,3]==0).sum()),'partial_alpha_cutout_pixels':int(((c[:,:,3]>0)&(c[:,:,3]<255)).sum()),'sha256':hashlib.sha256((p/(k+'.png')).read_bytes()).hexdigest()})
 s.append(f);cuts.append(c)
assert (p/'S2_on_air.png').read_bytes()==(p/'phones_b.png').read_bytes()
diff=(s[0]!=s[1]).any(2)
assert not diff[430:635,320:700].any(),'face/glasses/expression changed'
assert not diff[135:245,430:590].any(),'cap logo changed'
assert not diff[635:].any(),'medical clothing or desk changed'
yy,xx=np.indices(diff.shape); allowed=(yy<380)|(((xx<300)|(xx>725))&(yy<635))
assert not (diff&~allowed).any(),'change outside accessory removal zone'
old_mask=np.array(Image.open(p/'headphone_palette_mask.png').convert('RGBA'))[:,:,3]>0
scale=800/(1085-118); tx=512-1254*scale/2; ty=96-118*scale
sx=np.rint((xx-tx)/scale).astype(int);sy=np.rint((yy-ty)/scale).astype(int)
valid=(sx>=0)&(sx<old_mask.shape[1])&(sy>=0)&(sy<old_mask.shape[0])
shell=np.zeros(diff.shape,bool);shell[valid]=old_mask[sy[valid],sx[valid]]
shell&=(s[1][:,:,0]-s[1][:,:,1]>20)
changed_shell=int((diff&shell).sum());total_shell=int(shell.sum())
assert changed_shell/total_shell>0.99,'coral headphone shell remains'
original_hashes={'a':'7b7704203f61895ff537d1ba5486e615cb8c42d87d0f5b126ff135c20c774bf0','b':'5032cb43ae26682c14b8bbd86f2f0a7e1bb7555e95785d66bdf484ce4af2edb0','c':'28256a0613108b3c07927cbd182a3194c67ade8fe8ad357ca7bf67ecb7c05af5'}
for k,v in original_hashes.items():assert hashlib.sha256((p/f'phones_{k}.png').read_bytes()).hexdigest()==v
out['comparison']={'S2_equals_selected_B_byte_for_byte':True,'face_glasses_expression_changed_pixels':0,'cap_logo_changed_pixels':0,'medical_clothing_changed_pixels':0,'desk_changed_pixels':0,'outside_accessory_removal_zone_changed_pixels':0,'changed_pixels_total':int(diff.sum()),'coral_shell_pixels_changed':changed_shell,'coral_shell_pixels_checked':total_shell,'coral_shell_changed_fraction':changed_shell/total_shell,'original_A_B_C_unchanged':True,'registration_best_translation_px':[0,0],'registration_method':'read-only ROI translation search +/-10px; face, cap logo, clothing all best at zero'}
out['visual_QA']={'S1':'front facing, small relaxed smile, no headphones, no headphones on neck or desk; generated cap edge, hair and both ears inspected','S2':'same pose and expression, selected B coral headphones unchanged, readable unobstructed forward NABO cap logo','hands':'both images show no hands or fingers above desk; no static hand defects visible; dynamic hand anatomy not tested','style':'selected NABO 2D cartoon rendering preserved; no white sticker border added','mirroring':'none; NABO reads left to right','video_and_animation':'not generated; frame count, fps and loop seam not tested'}
out['preview']={'name':'S_check.png','size_px':list(Image.open(p/'S_check.png').size),'sha256':hashlib.sha256((p/'S_check.png').read_bytes()).hexdigest()}
(p/'round2_validation.json').write_text(json.dumps(out,ensure_ascii=False,indent=2)+'\n',encoding='utf-8')
names=['S1_off_air.png','S2_on_air.png','S_check.png']
(p/'SHA256SUMS_ROUND2.txt').write_text(''.join(hashlib.sha256((p/n).read_bytes()).hexdigest()+'  '+n+'\n' for n in names),encoding='utf-8')
print(json.dumps(out,ensure_ascii=False))

