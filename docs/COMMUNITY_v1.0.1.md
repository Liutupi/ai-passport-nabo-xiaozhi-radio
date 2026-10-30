# NABO FM v1.0.1 社区更新资料

本资料对应原作品 **1423 / nabo-fm** 的 v1.0.1 体验更新，使用封存 06r5 精确镜像，沿用既有标题与介绍主线。文案审阅基线为原公开 revision 3241；实际发布 / 审核状态以官方回执和社区页面为准。

Build / Host 通过；台 26 约 3 分钟短测及 2 轮主页往返通过，用户确认当前声音连续清楚。剩余完整验收由用户主动提前结束，三台各 600 秒和 20 次切台未完成；本页不宣称全部 QA 或长播通过。详见 [发行说明](RELEASE_FM_v1.0.1.md)。

## 中文

### 名称 / Title

NABO FM · 工牌收音机

### 介绍 / Description

让工牌，也有自己的电台时光。🎧

NABO 戴上耳机，当你的随身 DJ。听一首歌、跟一段新闻，或打开熟悉的地方台，让写字、做事、放空的片刻多一点声音陪伴。

• 想听就找：1,105 台内置目录，支持拼音首字母搜台。
• 喜欢就留下：双击中键收藏，独立列表随时找回，重启后还在。
• 按自己的节奏：北京时间待机小钟，1/3/5/10 分钟或关闭待机；也可定时停止收听。
• 三个按键就能开始：上下换台，中键播放/暂停，长按中键打开菜单。

v1.0.1 体验更新：MP3 是主要验证格式；部分台源、AAC/HE-AAC 与长时流畅度仍有已知限制，1,105 不代表全部台可播。播放电台时暂停小智语音。完整首装尚未实机验收。

展示图使用现有 NABO 原画与实际界面预览，非实机照片。AI 辅助开发与视觉排版。

### 使用方法 / Instructions

1. 完整安装会清空配网、激活和设置；保留数据先读升级说明。
2. 首次无网络记录会进入配网；换网用主页下键→工具→更多设置→添加网络。手机连接屏显 Xiaozhi-XXXX 热点，浏览器打开 http://192.168.4.1。
3. 选2.4GHz Wi-Fi，输密码点“连接”；网页显示“配置成功”且工牌联网后按OK返回，失败核对密码重试。绑定按屏幕指引。
4. 主页下键→工具→网络电台；上下换台，OK播放/暂停、双击收藏、长按菜单。
5. 播放时暂停语音；上键双击返回，长按停止电台回小智后再对话。重启可按保存状态续播。

### 本次更新 / Changelog

• 改善回到主页后再次收听时可能无声，以及退出电台后语音恢复的问题。
• 调整短时断流后的等待与声音过渡，让恢复播放更平缓。
• 改进连接失败或断网后的重试，减少反复启动播放。

## English

### 名称 / Title

NABO FM · Passport Radio

### 介绍 / Description

A little radio time, right on your AI Passport. 🎧

NABO puts on the headphones and becomes your pocket DJ. Tune in to music, news, or a familiar local station, and let a favorite voice keep you company.

• Explore a 1,105-entry station catalog with pinyin-initial search.
• Double-click the middle key to save a station across restarts; return through a dedicated favorites list.
• Choose a Beijing-time standby clock after 1/3/5/10 minutes, or disable the radio standby clock. A separate sleep timer can stop playback.
• Three-key controls: up/down to tune, middle to play/pause, hold middle for the menu.

v1.0.1 preview update: MP3 has the main device-test coverage. Some station URLs, AAC/HE-AAC and long-play reliability still have limitations. Catalog size is not a playback guarantee. Radio pauses voice chat. Fresh installation remains unverified.

Images use existing NABO artwork and real UI previews, not device photos. Development and visual layout used AI assistance.

### 使用方法 / Instructions

1. A full installation clears saved Wi-Fi, activation and settings. To retain data, read the upgrade guide first.
2. With no saved network, the device enters Wi-Fi setup. To change networks: Down on home → Tools → More settings → Add network. Join the Xiaozhi-XXXX hotspot shown on the device, then open http://192.168.4.1 in your phone's browser.
3. Select your 2.4 GHz Wi-Fi, enter its password and tap Connect. Once the page shows configuration success and the device is online, press OK on the device to return. If it fails, check the password and retry. Follow the screen for binding.
4. From home: Down → Tools → Internet radio. Up/Down changes station; tap OK to play/pause, double-click to favorite, and hold for the menu.
5. Radio pauses voice chat. Double-click Up to go back, or hold Up to stop radio and return to XiaoZhi before talking. Playback may resume after restarting according to the saved state.

### 本次更新 / Changelog

• Improved cases of missing audio when reopening the radio from home, and voice restoration after leaving the radio.
• Adjusted waiting and audio transitions after brief stream interruptions for a smoother return to playback.
• Improved retries after connection failure or network loss, reducing repeated playback restarts.

## 镜像、源码与展示素材

应用 SHA256：`282cf47d20af170dccbcaf0e6b06c84cbf73cea8e8752744f9a0f0463d90f836`（3,488,272 B）。完整构建镜像 SHA256：`14655b7580cd43d5e6735d65fd34ef5c3943a8ebf06fdd271cac617f1d8f51dd`（7,213,881 B）。源码：https://github.com/Liutupi/ai-passport-nabo-xiaozhi-radio 。发行包与社区预览须使用相同的 06r5 字节和上述双语字段。

使用 `docs/images/community-v1.0.1/` 的 01 封面及 02–05 四张介绍图，均为 900×1200、3:4。展示图由 NABO 原画和生产代码宿主预览组成，非实机照片；来源与图片校验值见该目录 manifest。第三方许可沿用仓库声明，NABO 美术权利独立于源码许可。
