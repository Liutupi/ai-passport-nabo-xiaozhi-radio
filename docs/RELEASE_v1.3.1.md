# v1.3.1 · 修复首次激活重启

## 更新

- **修复首次激活时重启**：从未激活或 NVS 被清空的工牌，配网后念完激活码会立刻发起 HTTPS 激活请求，此时音频任务还占着内存，TLS 握手失败导致设备重启一次。现在先等激活码念完、回收音频任务再请求，激活过程不再重启。
- 已激活的工牌不走这条路径，使用体验与 v1.3.0 相同。
- 已在 [AI Passport 玩法社区](https://ai-passport.folotoy.cn/plays/) 提交「NABO × 小智 · AI 对话 + 6 款小玩法」。

## 下载与安装

- `xiaozhi.bin`：应用镜像，偏移 `0x10000`。
- `ai-passport-nabo-xiaozhi-dive-v1.3.1.zip`：四个镜像、manifest、刷写工具、文档与字体许可证。bootloader、分区表和资源与 v1.0.0–v1.3.0 相同。
- `ai-passport-nabo-xiaozhi-dive-v1.3.1-full.bin`：从 `0x0` 刷写的完整固件，可用 [AI Passport 刷机工具](https://ai-passport.folotoy.cn/tools/web-flasher/) 的"本地固件刷机"安装。**会清空配网和激活信息**（以及各游戏成绩），装完需重新配网并在 xiaozhi.me 激活。
- `manifest.json`、`SHA256SUMS.txt`：校验和。

**应用 SHA256**：`22fb387f12e39247514ca60a0d85e1071b1cc7e4652b8df54c1afda1d657919a`

已安装 v1.0.0–v1.3.0：`python tools/flash_release.py --package . --mode upgrade --port 端口 --expected-mac 设备MAC`，只写应用，保留配网、激活和各项成绩。详见 [刷写说明](https://github.com/Liutupi/ai-passport-nabo-xiaozhi-dive/blob/v1.3.1/docs/FLASHING.md)，验证范围见 [验证记录](https://github.com/Liutupi/ai-passport-nabo-xiaozhi-dive/blob/v1.3.1/docs/VALIDATION.md)。

美术与原型：[nabo-dive](https://github.com/Liutupi/nabo-dive)。NABO 形象与美术不随 MIT 授权，见第三方说明。字体：思源黑体 Source Han Sans（Adobe，OFL 1.1）、Archivo Black、Space Mono（OFL 1.1），许可证随包提供。
