# NABO FM 第1轮：耳机三色方案
日期：2026-10-19。状态：本轮静态设计已完成，等待用户选择 A / B / C；尚未进入 S1/S2 定稿或动画制作。

先看 [phones_compare.png](phones_compare.png)。上排是实际小尺寸，下排是同一像素最近邻4倍放大。三张使用同一人物母图与同一耳机结构，只改变耳机壳配色。

| 方案 | 配色基准 | 取舍 |
|---|---|---|
| A 琥珀橙 | #ECA34D，炭灰耳垫 | 推荐。与深夜舞台、白帽白大褂分离清楚，也呼应琥珀调频刻度。 |
| B 珊瑚红 | #E97967，炭灰耳垫 | 更活泼，耳机最醒目；与 ON AIR 红灯的色系更接近，整屏阶段要关注视觉竞争。 |
| C 奶白配深灰 | #E8E0CE，炭灰耳垫 | 更安静、克制。耳垫与描边可分离轮廓，但与白帽接近，耳机辨识弱于 A/B。 |

这三项基准是配色导出的色相/饱和度目标；阴影、受光面与抗锯齿会产生其他像素颜色。它们不是单色图。

## 本轮文件
- phones_a.png / phones_b.png / phones_c.png：1024×1024，纯绿背景，完整播音台桌沿，供本轮选色查看。它们还不是已选定的 S2。
- phones_a_cutout.png / phones_b_cutout.png / phones_c_cutout.png：1024×1024 RGBA，无绿幕、无桌沿的透明人物检查版；桌沿锚点 (512,896)。
- phones_<a|b|c>_128.png：160×160 检查画布，背景 #161C2B；帽顶 y=10 到桌沿 y=138 实测128px。头梁跨在帽子上方，因此可见轮廓比128px稍高。
- phones_<a|b|c>_128_4x.png：以上检查画布最近邻放大4倍，640×640。
- phones_compare.png：2160×1000 三色并列比较图。
- QA_ROUND1.md / round1_validation.json：验收说明与实际PNG像素验证。
- SHA256SUMS.txt：三张最终方案和比较图的校验值。
- export_round1.ps1 / validate_round1.py：可复现导出与只读像素验证脚本。导出器拒绝覆盖同名输出。
- phones_a_raw.png：保留的1254×1254原始 imagegen 输出，不是定稿。
- review_v1/、review_v2/：修正前的本轮检查导出，仅留档，不参与选色。

## 人物与结构
人物母版为 ../reference/nabo-idle.png，来自同系列正式发布 v1.6.0 文档图；7份同名原图SHA256均为 C63575691D15220E8D6FAFDAB911A10767A5FC1C13925992DBD10E3A5E93D3FA。
睁眼第一格无损裁切为 ../reference/nabo-idle_open_source.png，作为 imagegen 身份参考。

原始设计使用 built-in imagegen，保留黑色圆框眼镜、棕发、白帽 NABO 与心电线、白大褂、藏青刷手服、听诊器与胸牌。之后只进行统一构图、绿幕清理和限定耳机壳蒙版的配色导出，没有程序重画人物或制造动画帧。
三色最终图的脸、帽标、服装及其余非耳机像素完全一致，透明剪影也完全一致。耳机为一条连续头梁、两侧连接杆及耳罩，左右均与耳朵位置对应；没有新增第二副耳机。帽标正向且无遮挡。
人物与母版是否完全符合用户心中的 NABO，仍应在本轮由用户目视认可；本说明不冒充用户已认可。

## 规格与验收摘要
1024×1024最终图完全不透明；人物外空白区为 #00FF00，y=896至底边全为 #2A2230。白帽最高亮部实测 y=94（要求约96），帽顶至桌沿802px；小尺寸图实测128px。
透明剪影有完全透明像素和正常半透明抗锯齿边缘，主体检测到的残余绿色为0。未做左右镜像。
PNG像素独立验证：三色耳机蒙版以外差异0；帽标ROI差异0；脸部ROI差异0；alpha轮廓逐像素一致；4倍检查图严格等于最近邻放大。
128px检查中眼睛、镜框、微笑和帽标可辨；胸牌细字不是此尺寸下的主要识别信息。外轮廓没有添加白色贴纸边。
本轮是静态设计，未验证手部动作、帧率、循环接缝或逐帧锚点。每动作64色属于后续游戏打包处理，当前概念图保留原始细节与抗锯齿，未声称达到64色。

## Library 保存结果
已按当前 Library skill 的 prepared-upload 流程尝试一次上传三张最终方案与比较图。
本机辅助程序明确失败：Library prepare_uploads is not available。
没有取得 library_file_id；没有宣称上传成功，也没有改用其他渠道重试。
本机所有交付可读，library_upload_receipt.json 保留失败记录。主对话需协调可用的 Library 环境后才能直接展示 Library 文件。

## 下一节点
请用户对照人物参考与比较图，选择 A / B / C，并确认人物与两侧耳机结构。
项目 CODEX_PROMPTS.md 明文要求：“分三轮，每轮结束你检查一次再进下一轮。”
本轮依用户最新要求停在选色节点，不制作 S1/S2、不生成视频、不联系外部服务。

## 实际 imagegen 提示词
Use case: identity-preserve. Use the most recent reference image shown in this conversation, the approved open-eyed NABO medical mascot on transparent/black displayed background. This is the edit target; do not invent a new character. Make one static 1024x1024 concept for NABO FM with warm amber orange over-ear headphones. Preserve exactly recognizable face, gray-blue eyes, black round glasses, brown hairstyle silhouette, white cap bearing blue NABO and heartbeat, proportions, white coat, navy scrubs, stethoscope and badge. Add only amber orange headphones (#ECA34D) with dark charcoal cushions and a band arching ABOVE the cap without obscuring any blue cap logo. Face forward, tiny relaxed smile, eyes open. Sit centered behind dark horizontal desk rectangle #2A2230 covering y=896..1024, hands fully hidden. Cap/headphone highest point near y=96, head about525px wide, chest hidden at desk. Plain exact green #00FF00 background, no shadow texture gradient or floor. No green on character. Clean 2D cartoon flat colors two-step cel shading, dark local-color outlines, no soft 3D shading or white sticker outline. No other objects, labels, mic, cup or captions. NABO letters readable left to right, never mirror. Keep all image boundaries clear. A static single design, not a sheet.

