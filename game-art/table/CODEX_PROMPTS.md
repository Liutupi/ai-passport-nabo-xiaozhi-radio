# 给 Codex 的提示词

分四轮，每轮结束你检查一次再进下一轮。每段提示词都能在新会话里单独使用。在 `nabo-table/` 目录下启动 Codex。

---

## 第 1 轮：NABO 正面半身造型（你来确认）

```
请先完整阅读 ART_BRIEF.md，这是美术规格，必须遵守。

任务：完全按 NABO 原本的形象，画 NABO 的正面半身造型，只做设计，不做动画。
不要换装，不要加荷官服、马甲、领结等服装改动；荷官身份之后靠动作和道具表达。

1. 出 1 张设计，放在 art/design/nabo_front.png：正面半身图（站在桌沿后，桌沿以下不画）、配色色块。
2. 另外缩小到 150 px 和 72 px 高，放在绒布色 #0E3B36 上，
   放大 4 倍（最近邻）展示，用来检查小尺寸下的辨识度和表情。
3. 画风：2D 卡通，平涂加两阶赛璐珞阴影，粗描边，不要软 3D 渲染。
4. 在 art/design/README.md 里说明为了小尺寸做了哪些简化。

只在 art/ 下工作，不要修改其他目录。完成后告诉我图的路径。
```

**你要检查的：** 是不是你的 NABO 本人；72 px 那张还认不认得出、表情看不看得清。

---

## 第 2、3 轮：已作废

骨骼拼接动画方案已在 2026-10-18 作废，改为「起始图 → 图生视频 → 处理成帧」。
动作镜头和要求见 `art/reference/SHOT_LIST.md`，可复制的视频提示词见 `art/reference/I2V_RESOURCE_REQUESTS_v1.txt`。
旧的骨骼提示词（含 --mirror、只画左指）不要再用：帽子上的 NABO 字样不能镜像。

---

## 第 4 轮：道具和配色

```
请先完整阅读 ART_BRIEF.md 第 7、8 节。

任务：
1. 导出道具：art/props/card_back.png（36×50 最终尺寸）、card_back_l.png（72×100）、
   cup.png（约 56 px 高）、trophy.png（约 40 px 高）。都按最终尺寸的 4 倍出图，
   并在 manifest.json 的 props 里写明 target_px。
   牌背带 NABO 主题的小图案，和 rig 里 NABO 手上的牌背保持同一设计。
2. 输出 art/palette/table.json，附 RGB565 值；需要微调起点配色时，说明原因。
3. 输出 art/preview/props_table.png：240×320 原尺寸，绒布背景上摆一行 5 张叠放的牌背、
   一个骰盅、72 px 的 NABO idle 第 1 帧，再放大 2 倍一份，用来检查整体搭配。
4. 跑一遍 ART_BRIEF 第 10 节验收清单，把结果写进 art/QA.md。

只在 art/ 下工作。
```

**你要检查的：** `props_table.png` 上牌背能不能数清张数、NABO 和绒布对比够不够。
