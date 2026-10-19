# NABO FM 镜头清单（给 Grok 图生视频）

## 怎么用

1. 每条镜头上传对应的起始图（`art/design/S1_off_air.png` 或 `S2_on_air.png`），粘贴“通用前缀 + 该条提示词”。
2. 时长选最短的（约 5–6 秒）。每条可以多生成几次，挑最稳的。
3. 原视频原样放进 `art/video/batch1/source/`，文件名用动作名（如 `groove.mp4`），不要剪、不要转码，我来处理。
4. 挑选标准（按重要性）：**脸和帽标不变形** > 镜头不动 > 回到起始姿势 > 动作好看。帽标字样糊了、眼镜变形、多出手指的直接重来。

## 通用前缀（每条都加在最前面）

```
2D cel-shaded cartoon animation, flat colors with two-step cel shading and dark outlines, matching the start image exactly. Locked-off static camera: no zoom, no pan, no camera shake. Solid flat pure green background (#00FF00), no shadows on the background. The character stays centered and seated behind the dark desk edge at the bottom of the frame; the desk edge never moves. Keep the character's face, round black glasses, brown hair, white cap with the "NABO" logo and heartbeat line, white doctor's coat, navy scrubs and stethoscope exactly the same in every frame. The cap logo must stay readable and never mirror. No new characters, no text, no extra objects except those described. Smooth animation with anticipation and ease-in/ease-out. The clip ends in exactly the same pose as the first frame.
```

## 必需

### idle_off — 停播待机（S1，循环）
```
The character sits calmly behind the desk with no headphones, breathing gently, shoulders rising and falling slightly. Blinks once in the middle. Sways very slightly left and right. Relaxed, friendly expression with a small smile. Returns to the exact starting pose.
```

### phones_on — 戴耳机（S1 开始，结束在 S2 姿势）
```
The character reaches down below the desk edge with both hands, lifts up a pair of headphones (the same headphones shown in the reference), and places them over the cap onto both ears, the headband going over the top of the cap without covering the NABO logo. Settles into a happy, ready-to-broadcast pose and holds it still for the last second.
```
> 如果 Grok 支持“结束帧”，把 `S2_on_air.png` 设为结束帧。不支持就照常生成，我来对齐。

### phones_off — 摘耳机（S2 开始，结束在 S1 姿势）
```
The character lifts the headphones off both ears with both hands, lowers them down below the desk edge and puts them away, then sits back in a calm relaxed pose without headphones and holds it still for the last second.
```
> 支持结束帧的话，结束帧用 `S1_off_air.png`。

### tune — 调台（S2，循环）
```
Wearing headphones, the character presses the left ear cup with the left hand and, with the right hand, reaches down to the desk edge and slowly turns an unseen knob back and forth. Eyes look upward, concentrating, as if searching for a radio station. Small head tilt. Returns to the starting pose.
```

### groove — 律动（S2，循环）
```
Wearing headphones, the character closes the eyes with a blissful smile and nods the head to a steady music beat, about two nods per second, shoulders bouncing gently with each nod. Hair bounces slightly behind the motion. Hands stay still. Returns to the starting pose.
```

### listen — 收听（S2，循环）
```
Wearing headphones, the character listens attentively to a talk radio program: one hand lightly holds the right ear cup, eyes open and interested, gentle slow nods, blinks once. Calm and subtle movement. Returns to the starting pose.
```

### sip — 喝咖啡（S2，单次）
```
Wearing headphones, the character reaches below the desk edge, picks up a plain warm-colored coffee mug with no text, takes a sip, closes the eyes and exhales a satisfied "ahh" with a content smile, then puts the mug back down below the desk edge and returns to the starting pose.
```

### love — 比心（S2，单次）
```
Wearing headphones, the character raises both hands in front of the chest and forms a heart shape with the fingers, winks one eye with a bright smile, holds for a moment, then lowers the hands back to the starting pose. Hands must have five fingers each.
```

### oops — 哎呀（S2，单次）
```
Wearing headphones, the character looks a bit embarrassed: scratches the side of the head (on the cap, not covering the logo) with one hand, shrugs the shoulders slightly and gives a sheepish awkward smile, then returns to the starting pose.
```

### doze — 打瞌睡（S2，循环）
```
Wearing headphones, the character is getting very sleepy: eyelids slowly droop, the head slowly sinks forward and down, then jerks back up awake with eyes half-open, then starts drooping again. Slow and cute. Returns to the starting pose at the end.
```

## 推荐

### wave — 招手（S1，单次）
```
The character looks at the viewer, raises the right hand and waves hello twice with a warm smile, then lowers the hand back to the starting pose.
```

### search — 找信号（S1，循环）
```
The character puts one hand above the eyes like a visor and looks to the left, then to the right, searching for something far away, slightly worried expression, then returns to the starting pose.
```

## 可选

### sing — 跟唱（S2，循环）
```
Wearing headphones, the character sings along happily to music, mouth opening and closing in rhythm, eyes closed, swaying the upper body gently left and right. Hands stay low. Returns to the starting pose.
```

## 进度表

| 动作 | 已生成 | 选定文件 | 备注 |
|---|---|---|---|
| idle_off | ✅ | `idle_off.mp4` | 通过（详见 PICKS.md） |
| phones_on | ✅ | `phones_on_v2.mp4` | v1 头梁盖住帽标→重做；v2 头梁擦过帽标下沿约 3 帧（详见 PICKS.md） |
| phones_off | ✅ | `phones_off.mp4` | 通过（详见 PICKS.md） |
| tune | ✅ | `tune.mp4` | 通过（详见 PICKS.md） |
| groove | ✅ | `groove.mp4` | 点头偏慢（详见 PICKS.md） |
| listen | ✅ | `listen.mp4` | 通过（详见 PICKS.md） |
| sip | ✅ | `sip.mp4` | 通过（详见 PICKS.md） |
| love | ✅ | `love.mp4` | 通过（详见 PICKS.md） |
| oops | ✅ | `oops.mp4` | 抬臂有拖影（详见 PICKS.md） |
| doze | ✅ | `doze.mp4` | 通过（详见 PICKS.md） |
| wave | ✅ | `wave.mp4` | 指尖偶有绿溢色（详见 PICKS.md） |
| search | ✅ | `search.mp4` | 通过（详见 PICKS.md） |
| sing | ✅ | `sing.mp4` | 通过（详见 PICKS.md） |
