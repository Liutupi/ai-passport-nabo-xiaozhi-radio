# 给 Codex 的提示词

分三轮，每轮结束你检查一次再进下一轮。每段都能在新会话里单独使用。在 `nabo-radio/` 目录下启动 Codex，并把 `nabo-idle.png` 复制到 `art/reference/nabo-idle.png`。

---

## 第 1 轮：耳机方案（你来选）

```
请先完整阅读 ART_BRIEF.md，这是美术规格，必须遵守。

任务：给 NABO 设计电台 DJ 用的耳机，只做设计，不做动画。
NABO 的身份和服装完全以 art/reference/nabo-idle.png 为准（白帽和 NABO 帽标、黑色圆框眼镜、
棕发、白大褂、藏青刷手服、听诊器、胸牌），一律不改，唯一新增的是一副耳机。

1. 出 3 个耳机配色方案，放在 art/design/phones_a.png、phones_b.png、phones_c.png。
   每张是 NABO 正面半身戴耳机的样子：坐在深灰播音台桌沿后，桌沿遮住胸口以下。
   头梁跨过帽子上方，不能挡住帽标的 NABO 字样和心电线。
   建议颜色：琥珀橙、珊瑚红、奶白配深灰，不能用绿色。
2. 每个方案再缩小到 128 px 高，放在 #161C2B 底色上，最近邻放大 4 倍展示，
   检查小尺寸下耳机和帽标是否清楚、表情能否看清。三个方案拼成 art/design/phones_compare.png。
3. 画风：2D 卡通，平涂加两阶赛璐珞阴影，深色外描边，不要软 3D 渲染。

只在 art/ 下工作，不要修改其他目录。完成后告诉我图的路径。
```

**你要检查的：** 是不是你的 NABO 本人；耳机颜色选哪个；128 px 那张帽标和表情是否清楚。

---

## 第 2 轮：两张起始图 S1 / S2（给 Grok 用）

```
请先完整阅读 ART_BRIEF.md 第 3、5 节。耳机定稿是 art/design/phones_<我选的字母>.png。

任务：出两张图生视频用的起始图，1024×1024 PNG。
- 背景纯绿 #00FF00，无阴影无渐变。
- 画面底部 y=896 到底边是一条纯色深灰播音台桌沿 #2A2230，NABO 坐在桌后，桌沿遮住胸口以下。
- NABO 居中，帽顶约在 y=96。左右各留出空间，伸手时不出画。
- 正面朝向观众，不要镜像。身上（包括耳机）不能有绿色。

1. art/design/S1_off_air.png：没戴耳机（耳机放在桌下看不见），双手自然放在桌沿后，放松微笑。
2. art/design/S2_on_air.png：戴着定稿耳机，其余姿势、构图、表情和 S1 尽量一致，
   双手同样放在桌沿后。
3. 两张都缩到 128 px 高放在 #161C2B 上做检查图 art/design/S_check.png（最近邻放大 4 倍）。

只在 art/ 下工作。
```

**你要检查的：** S1 和 S2 除了耳机以外是否几乎一样（位置、大小、表情）；绿幕纯不纯；帽标字样方向对不对。然后把 S1 / S2 交给 Grok，按 `art/reference/SHOT_LIST.md` 生成视频。

---

## 第 3 轮：道具和配色

```
请先完整阅读 ART_BRIEF.md 第 7、8 节。

任务：
1. 导出道具，透明背景，按最终尺寸 4 倍出图：
   art/props/mic.png（广播话筒，约 26×36 px，不含支架）、
   art/props/mug.png（约 20 px 高，和 S2 同画风，纯色杯身不写字，可以有小心电线图案）、
   art/props/headphones.png（定稿耳机平放，约 40 px 宽）、
   art/props/nabo_head_24.png（NABO 戴耳机的头像，24×24 px，另出 96×96 的 4 倍图）。
   在 art/manifest.json 的 props 里写明每个的 target_px。
2. 输出 art/palette/radio.json，附 RGB565 值；如果微调了起点配色，写明原因。
3. 输出 art/preview/props_radio.png：240×320 原尺寸，参考 art/reference/ui_mockup_240x320.png 的布局，
   把 S2 缩到 128 px 高放进舞台区、话筒放在右侧、头像放在左上，再放大 2 倍一份，检查整体搭配。
4. 跑一遍 ART_BRIEF 第 10 节验收清单，结果写进 art/QA.md。

只在 art/ 下工作。
```

**你要检查的：** `props_radio.png` 上 NABO、耳机和话筒放在一起顺不顺眼；话筒有没有挡住脸。
