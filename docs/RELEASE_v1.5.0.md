# v1.5.0 · NABO 牌桌多人版

## 新内容

- **2–4 人同桌**：封面新增「N 人」一项，按 OK 在 2 / 3 / 4 人之间切换，和局数一起保存在设备上（从旧版本升级默认 2 人）。工牌按 1 → 2 → 3 → 4 号传，玩家 3、4 是粉色和绿色。
- **21 点**：不爆的最高点数赢，**并列最高的都得一局**；全部爆牌或所有人点数相同算平局。打到目标局数时如果有人并列领先，继续加赛。
- **大话骰改为罚分制**：开盅后错的一方罚 1 分，先罚满 3 / 5 / 7 分的人输，被罚的人下一局先叫。起叫至少等于人数（三人 3 个、四人 4 个）。两人局胜负与以前等价，比分改为显示罚分。
- 三、四人时公开牌桌改成 2 × 2 座位；交接屏列出每人牌数与是否停牌；结算条写明多位赢家或谁罚分；结束画面显示每人分数。

![多人画面](https://raw.githubusercontent.com/Liutupi/ai-passport-nabo-xiaozhi-table/v1.5.0/docs/images/lcd-table-multi.png)

## 下载与安装

- `xiaozhi.bin`：应用镜像，偏移 `0x10000`。
- `assets.bin`：资源镜像，偏移 `0x400000`。
- `ai-passport-nabo-xiaozhi-table-v1.5.0.zip`：四个镜像、manifest、刷写工具、文档与字体许可证。
- `ai-passport-nabo-xiaozhi-table-v1.5.0-full.bin`：从 `0x0` 刷写的完整固件（SHA256 `1f77d8d89b8d9df4e7c1958ad60ba228681826e1de7152cc42d5850ac0db90d7`），可用 [AI Passport 刷机工具](https://ai-passport.folotoy.cn/tools/web-flasher/) 的"本地固件刷机"安装。**会清空配网和激活信息**（以及各游戏成绩），装完需重新配网并在 xiaozhi.me 激活。
- `manifest.json`、`SHA256SUMS.txt`：校验和。

**应用 SHA256**：`849a1ce6468c9f927ba94e951f74c5dbf1a9c1ec0da06bbdc8d63187e1f43e05`
**资源 SHA256**：`b7230904f977912bdb4b31b21bb47ad105c7e49e4a2f7a162664ba8c43107785`
**源码提交**：`0ef75e5`（本标签只在其后补充了版本文档，固件源码相同）

已安装本定制固件任一版本（v1.0.0–v1.4.0）：`python tools/flash_release.py --package . --mode upgrade --port 端口 --expected-mac 设备MAC`。upgrade 写应用和资源两个镜像，保留配网、激活和各项成绩。详见 [刷写说明](https://github.com/Liutupi/ai-passport-nabo-xiaozhi-table/blob/v1.5.0/docs/FLASHING.md) 与 [验证记录](https://github.com/Liutupi/ai-passport-nabo-xiaozhi-table/blob/v1.5.0/docs/VALIDATION.md)。

NABO 形象与美术（含牌桌的荷官动画）不随 MIT 授权，见第三方说明。字体：思源黑体 Source Han Sans（Adobe，OFL 1.1）、Archivo Black、Space Mono（OFL 1.1），许可证随包提供。
