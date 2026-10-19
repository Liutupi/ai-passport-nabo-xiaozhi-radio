from pathlib import Path
import shutil,subprocess,sys
art=Path(__file__).resolve().parents[1]
p=art/'tools'/'export_round3.py';src=p.read_text(encoding='utf-8-sig')
old="arr=np.array(out);arr[arr[:,:,3]<4]=0"
new="arr=np.array(out);arr[:,:,3][arr[:,:,3]>=250]=255;arr[arr[:,:,3]<4]=0"
assert old in src;src=src.replace(old,new);p.write_text(src,encoding='utf-8')
files=['props/mic.png','props/mug.png','props/headphones.png','props/nabo_head_24.png','props/nabo_head_96.png','props/nabo_head_24_4x_nearest.png','props/game/mic.png','props/game/mug.png','props/game/headphones.png','preview/props_radio.png','preview/props_radio_2x.png','preview/props_radio_rgb565.png','preview/props_sheet.png','palette/radio.json','manifest.json','delivery/round3_export_summary.json','delivery/round3_validation.json']
archive=art/'delivery'/'review_v2';archive.mkdir(exist_ok=False)
for rel in files:
    source=art/rel;target=archive/rel
    assert source.resolve().is_relative_to(art.resolve()) and target.resolve().is_relative_to(art.resolve())
    target.parent.mkdir(parents=True,exist_ok=True);shutil.move(str(source),str(target))
subprocess.run([sys.executable,str(p)],check=True)
subprocess.run([sys.executable,str(art/'tools'/'validate_round3.py')],check=True)

