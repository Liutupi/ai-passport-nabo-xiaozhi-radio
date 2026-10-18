## 新内容

- **谁是卧底，3–8 人同桌**：一台工牌传着看词、轮流描述、投票出局、揭晓身份。3–6 人安排 1 个卧底，7–8 人安排 2 个；264 组词，支持忘词私密重看、投票二次确认，人数独立保存。
- 牌桌封面增加「谁是卧底」；对小智说“打开谁是卧底”可直达人数设置。21 点和大话骰仍支持 2–4 人，NABO 继续当荷官。
- 修复牌桌直绘与配网提示同时刷新屏幕造成的 SPI 总线争用。本版保留小智、NABO 动画、跳一跳和设备设置，**移除节奏回声**。

![谁是卧底人数设置](https://raw.githubusercontent.com/Liutupi/ai-passport-nabo-xiaozhi-table/v1.6.0/docs/images/lcd-undercover-setup.png)

## 下载与安装

- `ai-passport-nabo-xiaozhi-table-v1.6.0.zip`：四个分区镜像、manifest、刷写工具、使用与验证说明、字体许可证。
- `ai-passport-nabo-xiaozhi-table-v1.6.0-full.bin`：从 `0x0` 刷写的完整固件，可用于 [AI Passport 网页刷机工具](https://ai-passport.folotoy.cn/tools/web-flasher/) 的本地固件刷机。**会清空配网、激活信息和游戏成绩**，安装后需重新配网并在 xiaozhi.me 激活。
- `xiaozhi.bin`：应用镜像，偏移 `0x10000`。
- `assets.bin`：资源镜像，偏移 `0x400000`，与 v1.5.0 相同。
- `manifest.json`、`SHA256SUMS.txt`：镜像信息与 SHA256 校验值。

已有本系列定制固件（v1.0.0–v1.5.0）且希望保留数据：解压 ZIP，按 [刷写说明](https://github.com/Liutupi/ai-passport-nabo-xiaozhi-table/blob/v1.6.0/docs/FLASHING.md) 使用 `tools/flash_release.py --mode upgrade`，填写自己的端口和设备 MAC；工具写入应用与资源并校验保留区域。不要把 ZIP 当作完整固件直接刷入。

**应用 SHA256**：`de9ff6def9c9e49464d77607aca6a53d4e162f8da387dc2e0f379106101d7327`

**资源 SHA256**：`b7230904f977912bdb4b31b21bb47ad105c7e49e4a2f7a162664ba8c43107785`

发布使用已刷写真机的现成镜像，未重新编译。已完成 5 人、8 人卧底至结果揭晓、2 人 21 点和大话骰各一局，以及 10 轮牌桌进出压力测试，无重启、assert 或显示总线错误。验证由串口自动操作驱动；实体按键/声音人工试玩与联网语音回归不在本轮记录内。详见 [验证记录](https://github.com/Liutupi/ai-passport-nabo-xiaozhi-table/blob/v1.6.0/docs/VALIDATION.md)。

仅适配 FoloToy AI Passport（ESP32-C3 / 8 MB）。本项目为社区独立定制项目；NABO 形象与美术不随源码 MIT 授权，第三方说明与字体许可证随包提供。
