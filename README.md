# AI Passport · NABO FM × 小智

ESP32-C3 / 8 MB FoloToy AI Passport 的电台专用固件，从牌桌 v1.6.0 分出。保留 NABO 小智动画和设备设置，电台采用午夜琥珀配色、霞鹜文楷台名、思源黑体小字，以及原 NABO 的 13 个 DJ 动作。

**2026-10-19 开发检查点：已通过 ESP-IDF 6.0.2 构建和宿主回归，尚未刷机验收，尚未发布正式 Release。** 图为同一套生产绘制代码在电脑上的输出，不能当作实机截图。

![NABO FM 生产代码画面预览](docs/images/radio/contact-sheet.png)

已接入：

- 1,105 台内置台表；全国、收藏、最近、类型、各地区波段；拼音首字母搜索；可手动选择本地地区。
- 停手 500 ms 后连接；下键长按加速扫台；上键单击先移动，双击撤回并返回。
- OK 单击播放/暂停，双击收藏，长按菜单。列表每屏七台；收藏和最近使用稳定 ID 保存。
- 13 个动作、247 帧的 NABO DJ 动画；8 行条带直绘，按条带校验值跳过未变画面。
- 32 KiB 压缩数据缓冲；Helix MP3、Espressif ADTS AAC 接入；半带 FIR、慢速响度均衡、高通、淡入/暂停收尾和限幅；输出跟随台的采样率。
- 定时关闭、最后一分钟渐弱、闲置一分钟夜间时钟、任意键只唤醒、换台沙沙声开关。
- `self.radio.search/play/next/random/pause/resume/set_volume/now_playing/favorite/sleep_timer` 设备工具；当前电台与小智切换音频所有权，播放中同时语音和 20% 压低尚未实现。

待完成：国内网络下的逐台复测、AAC 兼容性、自动 IP 地区定位、在线台表更新、边播边语音，以及真实硬件上的音质/内存/长时间连播验证。HLS、ICY 曲名、通用点歌、离线助眠声和 DLNA 仍按原设计后续分期处理。

[使用指南](docs/USER_GUIDE.md) · [构建](docs/BUILD.md) · [交接](docs/HANDOFF.md) · [验证](docs/VALIDATION.md) · [刷写](docs/FLASHING.md) · [许可](THIRD_PARTY_NOTICES.md)

```powershell
# 已初始化 ESP-IDF 6.0.2 的终端
python tools/build.py
# GCC/G++ 在 PATH；Windows 的 CXX 使用 g++，避免反斜杠被 shlex 解释
python tools/run_tests.py
```

动画、子集字体和台表均已随源码放在板型的 `radio_assets/`，正常编译不需要外部美术目录。重新制作资源时才需要设计输入和源字体。NABO 形象和美术不随源码的 MIT 许可授权。

分区及 `ai-passport-nabo` 板型身份保持兼容；升级必须同时写应用和资源。使用带 MAC、备份和写后检查的工具，勿直接用标准 OTA 或整片擦除。
