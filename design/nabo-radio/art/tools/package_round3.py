from pathlib import Path
import json,re,hashlib,zipfile
art=Path(__file__).resolve().parents[1];project=art.parent;delivery=art/'delivery'
m=json.loads((art/'manifest.json').read_text(encoding='utf-8'))
def write(p,s):
    if p.exists():raise RuntimeError('Refusing overwrite '+str(p))
    p.write_text(s,encoding='utf-8')
labels={'mic':'广播话筒（无支架）','mug':'无字琥珀马克杯','headphones':'B珊瑚红平放耳机','nabo_head_24':'NABO戴B耳机头像'}
table='|道具|文件|导出像素|游戏目标像素|\n|---|---|---|---|\n'
for k,s in m['props'].items():table+='|'+labels[k]+'|'+s['file']+'|'+'x'.join(map(str,s['export_px']))+'|'+'x'.join(map(str,s['target_px']))+'|\n'
readme="""# NABO FM B · 第三轮静态美术交付
本轮道具/配色/manifest/预览与QA完成。已选B和已确认S1/S2哈希不变，未改游戏逻辑、固件或skill。

## 文件与尺寸
"""+table+"""
头像另有96x96高分辨率版和24版最近邻4倍检查；其他三件均为目标尺寸4倍，并附props/game原尺寸版。
全部为真实RGBA透明PNG。话筒有效轮廓约22x35目标像素，画布26x36；耳机宽约40px，高35px，保持最终平放构图的比例。
接近不透明主体已归一化到255，保留AA边缘。24头像外缘仅有低alpha抗锯齿，四角完全透明。
话筒是灰色网罩配琥珀圈，无支架。马克杯暖琥珀纯色、右把手，无字/蒸汽/碟子。耳机沿用B珊瑚色和深灰垫，单头梁、双向上耳垫，连接自然。
头像直接裁取已验收S2，无重绘。24px主要识别白帽、眼镜、棕发和珊瑚耳机，不将帽标逐字可读作为小头像标准。

## 配色与预览
palette/radio.json保留原10色，逐项附RGB565整数与十六进制值；增加B/耳垫/视频桌沿角色色。manifest.json列明target_px、导出文件、源图与布局。
preview/props_radio.png为240x320；props_radio_2x.png为严格最近邻480x640；props_radio_rgb565.png为色彩模拟；props_sheet.png为道具总览。
沿用参考分区：舞台约y18-150、控制台150-178、台名178-222、刻度222-282、提示282-320。
S2按约128px帽顶到桌沿显示；话筒画布(184,92,26,36)，与脸部重叠0、水平间隔34px；24头像在(5,22)左上。
支架、电平灯、旋钮、ON AIR及心形只在静态预览中程序绘制，不是游戏代码。预览没有接入电台或播放声音。

## 来源与验收
三件新道具由built-in imagegen生成，最终原始透明PNG在props/source；实际全部提示词在IMAGEGEN_PROMPTS.json。
头像和人物复用用户已验收参考，未使用外部库存图。仅做规格裁切缩放、主体alpha归一化和耳机壳色彩校准，不改变已验收人物。
像素验证passed：尺寸/透明通道正确；四件主体绿色溢色0；RGB各不超过64色，AA透明度独立保留；RGB565逐项正确；2倍预览最近邻一致；B/S1/S2原哈希不变。
完整检查见../QA.md及round3_validation.json。字体由本机已有字体栅格化，包内不含字体文件。

## 下一步
NEXT_ANIMATION_ASSETS.md列出13条原视频和88目标帧，ANIMATION_PROMPTS.md提供每条完整提示词；原SHOT_LIST保持不变。
当前0条源视频、0个动作完成，没有循环接缝/动态手部/帧率/逐帧锚点通过声明。未来sip须使用本包无字琥珀杯设计。
戴/摘耳机按ART_BRIEF例外使用S1->S2、S2->S1，准备提示词去掉了与例外冲突的回到相同首帧句，其余返回对应起始姿势。
未发起外部视频生成、未联系Grok/Claude，也未调用付费API。
"""
write(delivery/'README_ROUND3.md',readme)
qa="""# NABO FM 美术QA（第三轮）
静态资产验收通过，动画项目保持待验证。

## ART_BRIEF第10节
- [x] 128px尺度、#161C2B上可识别NABO和表情；整屏已目视检查。
- [x] 耳机不挡主NABO/心电线帽标；使用已确认S2，不重画。
- [ ] 循环无缝及listen/groove相同第1帧：无源视频和动画帧；已指定两者第0帧均为S2，待实际验证。
- [ ] 所有游戏帧同锚点：尚无游戏帧；静态起始图y896一致，后续256方形帧锚点必须(128,224)。
- [x] 无镜像，NABO字样正向。
- [x] 深色轮廓，无新增白贴纸边；帽子、大褂和高光本色保留。
- [x] 沿用已验收绿幕图；新道具透明且主体绿色溢色0。
- [x] 新道具imagegen生成，人物/头像用用户已验收自有参考；未下载第三方图。

## 本轮其他检查
RGBA、尺寸、透明背景、64色RGB和半透明AA通过；24头像边缘低alpha抗锯齿，未出现不透明截断。
话筒无支架、杯无字、耳机一条梁和两只朝上耳垫且连接自然，头像无人物重绘。
240x320预览与480x640最近邻2倍匹配；话筒与脸重叠0、间隔34px；头像左上。
原10色未改，RGB565逐项正确。B/S1/S2哈希未改。
原视频数量0，13条动画的手部/循环/帧率和最终游戏锚点尚未验证。
证据：delivery/round3_validation.json。
"""
write(art/'QA.md',qa)
actions=m['animation_spec']['actions']
nxt="""# 后续动画资源清单
当前0条源视频、0动作完成。以下为目标，不是已交付帧。
12fps、256x256 RGBA、锚点(128,224)、每动作64色。
10必需68帧；2推荐12帧；1可选8帧，合计13动作88帧。

|原视频名|起始/变化|目标帧|循环|优先级|状态|
|---|---|---|---|---|---|
"""
for a in actions:nxt+=f"|{a['name']}.mp4|{a['start']}|{a['target_frames']}|{'是' if a['loop'] else '否'}|{a['priority']}|待源视频|\n"
nxt+="""
以design/S1_off_air.png或S2_on_air.png作起始图，不用带UI预览。
约5-6秒源视频交回art/video/batch1/source，原样保存不裁不转码；多次生成加_v2/_v3，PICKS.md记选取。
选一个完整动作/循环处理成预算帧，不将多次循环粗暴压进短段。先检查脸/眼镜/帽标、手部与镜头/桌沿稳定，再检查流畅和接缝。
S1系接idle_off；S2系接listen/groove，两者第0帧均需S2相同姿势。
sip须复用props/mug.png，戴摘耳机须B珊瑚色。静态双手藏桌后不等于动态手部已验收。
本交付未执行外部生成。
"""
write(delivery/'NEXT_ANIMATION_ASSETS.md',nxt)
shot=(art/'reference'/'SHOT_LIST.md').read_text(encoding='utf-8');fence=chr(96)*3
prefix=re.search(r'## 通用前缀.*?'+fence+r'\n(.*?)\n'+fence,shot,re.S).group(1)
shots=dict(re.findall(r'^### ([a-z_]+).*?\n'+fence+r'\n(.*?)\n'+fence,shot,re.S|re.M));assert len(shots)==13
prepared="""# 每条完整动画提示词（待源视频）
原SHOT_LIST未改。本文件仅文本，没有执行生成。戴摘耳机按ART_BRIEF明确例外处理不同首尾姿势；其他镜头返回原起始姿势。
"""
for a in actions:
    name=a['name'];pfx=prefix.replace('no text,','no new lettering (keep the existing NABO cap logo),')
    if name in ('phones_on','phones_off'):pfx=pfx.replace('The clip ends in exactly the same pose as the first frame.','')
    extra=' Match the approved muted coral B headphones exactly as in S2, never green.'
    if name=='sip':extra+=' Use the plain amber mug in props/mug.png, right handle, no text/logo.'
    prepared+=f"\n## {name}\n起始/变化：{a['start']}；目标{a['target_frames']}帧，12fps。\n\n~~~text\n{pfx.strip()}\n{shots[name]}\nKeep the first frame exactly equal to the supplied approved start image before the action begins. Keep face and cap logo stable.{extra}\n~~~\n"
write(delivery/'ANIMATION_PROMPTS.md',prepared)
rel=['manifest.json','QA.md','palette/radio.json','design/phones_b.png','design/S1_off_air.png','design/S2_on_air.png','design/S1_off_air_cutout.png','design/S2_on_air_cutout.png','design/S_check.png','reference/nabo-idle.png','reference/SHOT_LIST.md','reference/ui_mockup_240x320.png','reference/layout_annotated_3x.png']
rel += [str(p.relative_to(art)).replace('\\','/') for p in (art/'props').rglob('*.png')]
rel += [str(p.relative_to(art)).replace('\\','/') for p in (art/'preview').glob('*.png')]
rel += ['tools/export_round3.py','tools/validate_round3.py','delivery/README_ROUND3.md','delivery/NEXT_ANIMATION_ASSETS.md','delivery/ANIMATION_PROMPTS.md','delivery/IMAGEGEN_PROMPTS.json','delivery/round3_validation.json']
members=[(project/r,'nabo-radio/'+r) for r in ['ART_BRIEF.md','CODEX_PROMPTS.md','DESIGN.md']]+[(art/r,'nabo-radio/art/'+r) for r in rel]
for p,n in members:assert p.is_file() and '..' not in Path(n).parts and not Path(n).is_absolute()
checks=''.join(hashlib.sha256(p.read_bytes()).hexdigest()+'  '+n+'\n' for p,n in members);write(delivery/'SHA256SUMS_ROUND3.txt',checks)
members.append((delivery/'SHA256SUMS_ROUND3.txt','nabo-radio/art/delivery/SHA256SUMS_ROUND3.txt'))
out=delivery/'NABO_FM_B_round3_art_pack.zip'
if out.exists():raise RuntimeError('Package exists')
with zipfile.ZipFile(out,'w',zipfile.ZIP_DEFLATED,compresslevel=6) as z:
    for p,n in members:z.write(p,n)
with zipfile.ZipFile(out) as z:
    assert z.testzip() is None and len(z.namelist())==len(members)
pack={'status':'passed','file':out.name,'file_size_bytes':out.stat().st_size,'sha256':hashlib.sha256(out.read_bytes()).hexdigest(),'members':len(members),'crc_all_members_passed':True,'animations_included':0,'source_videos_included':0,'new_props':4,'approved_sources_unchanged':True}
write(delivery/'package_validation.json',json.dumps(pack,ensure_ascii=False,indent=2)+'\n')
print(json.dumps(pack,ensure_ascii=False))

