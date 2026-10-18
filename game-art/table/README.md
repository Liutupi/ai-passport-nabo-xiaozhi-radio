# NABO 牌桌 · 美术与原型

固件见本仓库根目录；这里是《NABO 牌桌》的设计、美术流程和浏览器原型。

| 内容 | 位置 |
|---|---|
| 玩法与界面设计 | [DESIGN.md](DESIGN.md) |
| 美术规格（给 Codex） | [ART_BRIEF.md](ART_BRIEF.md)、[CODEX_PROMPTS.md](CODEX_PROMPTS.md) |
| 镜头清单、起始图、图生视频提示词 | [art/reference/](art/reference/)（[SHOT_LIST.md](art/reference/SHOT_LIST.md)） |
| 造型设计稿（选定 A：梅紫经典） | [art/design/](art/design/) |
| 图生视频源片（Grok Imagine） | [art/video/](art/video/) |
| 进度看板与取用片段 | [ART_STATUS.md](ART_STATUS.md) |
| 固件移植计划与各阶段记录 | [FIRMWARE_PLAN.md](FIRMWARE_PLAN.md) |
| 浏览器原型 | [prototype/index.html](prototype/index.html)（需用本地 HTTP 服务打开，例如 `python3 -m http.server -d prototype`） |

## 从视频到固件

```sh
python3 game-art/table/tools/clip_to_sprites.py     # 按 tools/clips.json 截取、抠背景、缩放、对齐 → game-art/table/sprites/
python3 game-art/table/tools/build_proto_atlas.py   # 原型用的精灵条 → prototype/assets/nabo_sprites.js
python3 tools/generate_table_sprites.py             # 固件用的精灵包 → table_assets/table_sprites.bin
```

需要 ffmpeg、Pillow、NumPy。`sprites/` 是生成目录，不入库。Codex 的中间处理结果、质检重拍、失败的骨骼方案只保留在作者本地，没有入库。

NABO 形象与这些美术素材不随源码的 MIT 许可证授权，见根目录 [THIRD_PARTY_NOTICES.md](../../THIRD_PARTY_NOTICES.md)。
