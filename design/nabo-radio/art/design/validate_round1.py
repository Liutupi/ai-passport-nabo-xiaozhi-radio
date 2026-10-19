from pathlib import Path
import json, hashlib
import numpy as np
from PIL import Image
p=Path(__file__).parent
stats=json.loads((p/'round1_metrics.json').read_text(encoding='utf-8'))
out={'status':'passed','scope':'Round 1 static headphone color designs only','checks':{},'variants':[]}
finals=[];cuts=[]
for key in 'abc':
    f=np.array(Image.open(p/f'phones_{key}.png').convert('RGBA')).astype(np.int16)
    c=np.array(Image.open(p/f'phones_{key}_cutout.png').convert('RGBA')).astype(np.int16)
    n=np.array(Image.open(p/f'phones_{key}_128.png').convert('RGBA'))
    z=np.array(Image.open(p/f'phones_{key}_128_4x.png').convert('RGBA'))
    assert f.shape==(1024,1024,4) and c.shape==f.shape
    assert (f[:,:,3]==255).all()
    assert (f[896:]==[42,34,48,255]).all()
    transparent=(c[:896,:,3]==0)
    assert (f[:896][transparent]==[0,255,0,255]).all()
    assert n.shape==(160,160,4) and z.shape==(640,640,4)
    assert np.array_equal(z,np.repeat(np.repeat(n,4,0),4,1))
    green=(c[:,:,3]>0)&(c[:,:,1]>80)&(c[:,:,1]>c[:,:,0]*1.25+15)&(c[:,:,1]>c[:,:,2]*1.25+15)
    assert int(green.sum())==0
    white=(c[80:140,480:545,0]>220)&(c[80:140,480:545,1]>220)&(c[80:140,480:545,2]>220)&(c[80:140,480:545,3]>200)
    cap_top=int(np.where(white)[0].min()+80)
    assert abs(cap_top-96)<=4
    w=(n[9:40,70:90,0]>220)&(n[9:40,70:90,1]>220)&(n[9:40,70:90,2]>220)
    native_cap_top=int(np.where(w)[0].min()+9)
    assert 138-native_cap_top==128
    yy,xx=np.where(c[:,:,3]>0)
    bbox=[int(xx.min()),int(yy.min()),int(xx.max()+1),int(yy.max()+1)]
    assert abs((bbox[0]+bbox[2])/2-512)<3
    assert (c[896:,:,3]==0).all()
    out['variants'].append({'id':key,'size_px':[1024,1024],'cap_top_measured_y':cap_top,'cap_to_desk_measured_px':896-cap_top,'native_cap_top_y':native_cap_top,'native_desk_y':138,'native_cap_to_desk_measured_px':138-native_cap_top,'subject_bbox':bbox,'alpha_zero_pixels':int((c[:,:,3]==0).sum()),'alpha_partial_pixels':int(((c[:,:,3]>0)&(c[:,:,3]<255)).sum()),'green_subject_pixels':0,'nearest_neighbor_4x_exact':True,'sha256':hashlib.sha256((p/f'phones_{key}.png').read_bytes()).hexdigest()})
    finals.append(f);cuts.append(c)
mask=np.array(Image.open(p/'headphone_palette_mask.png').convert('RGBA'))[:,:,3]>0
H,W=mask.shape
scale=stats['scale'];tx=512-1254*scale/2;ty=96-118*scale
grid_y,grid_x=np.indices((1024,1024))
sx=np.rint((grid_x-tx)/scale).astype(int);sy=np.rint((grid_y-ty)/scale).astype(int)
allowed=np.zeros((1024,1024),bool)
for dy in range(-4,5):
    for dx in range(-4,5):
        ix=sx+dx;iy=sy+dy
        valid=(ix>=0)&(ix<W)&(iy>=0)&(iy<H)
        allowed[valid]|=mask[iy[valid],ix[valid]]
max_diff=0
for k in (1,2):
    diff=(finals[k]!=finals[0]).any(2)
    assert np.array_equal(cuts[k][:,:,3],cuts[0][:,:,3])
    assert not (diff&~allowed).any(),f'difference outside headphone mask: {(diff&~allowed).sum()}'
    assert not diff[135:245,430:590].any()
    assert not diff[430:800,320:700].any()
    max_diff=max(max_diff,int(diff.sum()))
out['checks']={'identical_alpha_silhouette_all_variants':True,'pixel_differences_outside_headphone_mask':0,'cap_logo_region_differences':0,'face_region_differences':0,'background_hex':'#00FF00','desk_hex':'#2A2230','desk_y':896,'maximum_changed_pixels_in_headphones':max_diff,'not_mirrored':'visual inspection: NABO reads left to right','earphone_structure':'visual inspection: one arch, two connected cups at ears; same geometry in A/B/C','hands':'hidden behind desk; animation hand anatomy not assessed','animation_loop_anchor_fps':'not applicable: no animation produced in Round 1','palette_64_colors':'not applied: per-action packaging step deferred'}
out['compare']={'filename':'phones_compare.png','size_px':list(Image.open(p/'phones_compare.png').size),'sha256':hashlib.sha256((p/'phones_compare.png').read_bytes()).hexdigest()}
(p/'round1_validation.json').write_text(json.dumps(out,ensure_ascii=False,indent=2)+'\n',encoding='utf-8')
names=[f'phones_{k}.png' for k in 'abc']+['phones_compare.png']
(p/'SHA256SUMS.txt').write_text(''.join(hashlib.sha256((p/name).read_bytes()).hexdigest()+'  '+name+'\n' for name in names),encoding='utf-8')
print(json.dumps(out,ensure_ascii=False))

