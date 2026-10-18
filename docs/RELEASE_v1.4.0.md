# v1.4.0 · NABO 牌桌

## 新内容

- **NABO 牌桌**：两个人传着一台工牌玩的 21 点和大话骰，NABO 当荷官。换人时 NABO 捂眼，接手的人按 OK 才看到自己的牌或骰子。工具菜单第四项，或对小智说“打开牌桌”。
- **开机即玩**：小智还没联网时也能进工具菜单玩，退出后小智接着启动。
- 包含 v1.3.1 的首次激活修复。
- 为腾出空间，移除了深潜、摇骰子、功德木鱼和电脑快捷键。需要它们请继续使用 [深潜版](https://github.com/Liutupi/ai-passport-nabo-xiaozhi-dive/releases) 或 [电台版](https://github.com/Liutupi/ai-passport-nabo-xiaozhi/releases)。

## 下载与安装

- `xiaozhi.bin`：应用镜像，偏移 `0x10000`。
- `assets.bin`：资源镜像，偏移 `0x400000`（本版含牌桌素材）。
- `ai-passport-nabo-xiaozhi-table-v1.4.0.zip`：四个镜像、manifest、刷写工具、文档与字体许可证。
- `ai-passport-nabo-xiaozhi-table-v1.4.0-full.bin`：从 `0x0` 刷写的完整固件，可用 [AI Passport 刷机工具](https://ai-passport.folotoy.cn/tools/web-flasher/) 的"本地固件刷机"安装。**会清空配网和激活信息**（以及各游戏成绩），装完需重新配网并在 xiaozhi.me 激活。
- `manifest.json`、`SHA256SUMS.txt`：校验和。

**应用 SHA256**：`b9502231ad4d8b2056e91c567858826fbc91c30fa25dcdb60024c08892e97f4b`
**资源 SHA256**：`4653e4b98e64dbcebee5d42a5586db0683cad9c81421a1f2e44e1d13aaccb8b6`

已安装本定制固件任一版本（v1.0.0–v1.3.1）：`python tools/flash_release.py --package . --mode upgrade --port 端口 --expected-mac 设备MAC`。本版的 upgrade **写应用和资源**两个镜像，保留配网、激活和各项成绩。从牌桌版切回旧版本请用旧包的 install 模式，详见 [刷写说明](https://github.com/Liutupi/ai-passport-nabo-xiaozhi-table/blob/v1.4.0/docs/FLASHING.md)，验证范围见 [验证记录](https://github.com/Liutupi/ai-passport-nabo-xiaozhi-table/blob/v1.4.0/docs/VALIDATION.md)。

NABO 形象与美术（含牌桌的荷官动画）不随 MIT 授权，见第三方说明。字体：思源黑体 Source Han Sans（Adobe，OFL 1.1）、Archivo Black、Space Mono（OFL 1.1），许可证随包提供。
