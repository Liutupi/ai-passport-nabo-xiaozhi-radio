# 维护交接



## 2026-10-19 v1.0.0 已公开发布；社区展示优化已提交

- GitHub 仓库已按用户明确同意改为 **PUBLIC**，main 已更新；首版体验 Release：<https://github.com/Liutupi/ai-passport-nabo-xiaozhi-radio/releases/tag/v1.0.0>，标记 pre-release。标签源码提交 `2767690086287eeeb2b0cc1e7c428b6f171a72ec`。
- 四个公开附件：完整 BIN（7,213,881 B）、分区 ZIP（5,976,665 B）、manifest、SHA256SUMS。四个 GitHub 服务端 SHA256 均与本地一致。完整 BIN SHA256 `c8096177a4b99a3199bb4dcf2f36a7c428ce8e11c0f5aa0e6ea394f58730dbad`，应用仍为精确 03；本轮没有操作设备。
- 社区新作品 **1423 / NABO FM · 工牌收音机** 已提交。用户又要求优化图片与介绍，已用五张 900×1200 原创矢量排版海报替换直接截图，并重写中英文简介，重新提交审核。固件沿用已上传的 v1.0.0。审核通过前不能称公开上线。
- 本轮用户已明确同意社区协议、发布相关权利声明及平台必要的非独占免费许可。未生成或分享社区授权码。公开素材是 NABO 原画及宿主界面预览，不是工牌照片；未上传 NVS、备份或原始日志。
- 展示文案 `docs/COMMUNITY_v1.0.0.md`；海报 PNG/SVG 与来源校验 `docs/images/community-v1.0.0/`；可编辑排版脚本 `tools/render_community_posters.py`。公开下载包和固件镜像不因宣传图更新而替换。
- 私有发布回执 `private/publication/v1.0.0/receipt.json`。对应 ELF/MAP/配置等仍在封存候选 03 目录。AAC、失效台源、长播等已知边界不变。


## 2026-10-19 首版公开发布准备：main 恢复精确 03

用户已授权更新 GitHub、发布首版固件及社区作品，并明确同意仓库由私有改为公开（含源码、美术及提交历史）。发行版本为 **NABO FM v1.0.0 首版体验固件**，使用封存的候选 03 应用，不重新构建、不再次刷设备。

- main 的全部候选 03 快照文件已逐一 SHA256 对照恢复；完整宿主回归通过。应用仍为 `9360db685bd23a5b99b9fb8196ab1cf2e1db60b0ed9f44b59251f1cec661e44b`。发行源码及验证边界见 `validation/radio-v1.0.0.json`。
- 04/05 实验已保存在本地分支 `work/audio-candidate05-diagnostics`（`0d98f0e`），未作为本次发布源码。此前“工作区仍是 05”的记录已成为历史；不要把该分支合并后冒充 03。
- 完整 BIN 使用构建 bootloader、匹配分区表、精确 03 应用和已部署的 0.2.6 资源组合；空隙为 FF，不含工牌 NVS/备份。**从 0x0 写完整 BIN 会清除配网、激活及用户设置。** 分区 ZIP 另带受保护刷写工具；bootloader 字节不匹配仍拒绝升级，不绕过。
- 本次没有在空白设备重刷完整组合；仅做镜像、布局、校验和、空白 NVS 区及包许可检查。真实设备继续保持回退后的候选 03 和原用户数据。
- 03 MP3 多采样率、收藏列表和待机时钟有实机证据；AAC/HE-AAC、部分失效台、长时流畅度及全部 1,105 台仍未通过。GitHub 标记体验版 / pre-release，不承诺全部台流畅。
- 社区介绍图是生产绘制代码宿主预览，非实机照片，注明 AI 辅助开发；完整使用与安装边界见 `docs/RELEASE_FM_v1.0.0.md`。

以下各节为此前候选开发和刷写历史，以本节及精确镜像校验值为准。


## 2026-10-19 当前运行精确候选 03

用户授权 05 失败就回退 03。05 分配及首帧解码通过，但持续输出停止，已按授权回退精确 03，全部用户数据保护区读回一致。当前 03 已恢复持续解码；已向用户明确当前版本并保持 03。源码仍包含未提交的 05，不要用工作区重建冒充正在运行的镜像。根 `HANDOFF.md` 最新节记录实际身份、失败边界及私有校验路径。以下均为此前阶段历史。

## 2026-10-19 当前设备与修复候选（05 刷写前历史）

当前为候选 04，应用写入及数据保留通过，但 MP3 工作区因连续内存不足启动失败，已返回主页。候选 05 已拆分缓冲并通过碎片堆/完整回归及 C3 构建，尚未授权/刷写。具体 05、必要时回退 03 的身份与流程均在根 `HANDOFF.md` 最新节。以下条目是此前阶段记录。

## 2026-10-19 本机最新接续（候选 04 刷写前历史）

当前设备为候选 03（应用 `9360db685bd2…`），仅写应用及全部用户数据保护校验通过。用户确认声音增大且无爆音，收藏列表和待机时间正常，并要求再提高音量。候选 04（`f145773e2b31…`）构建/宿主检查已通过，尚未批准/刷写；具体身份、实测边界及下一步见根 `HANDOFF.md` 最新节。AAC 内存问题、部分失效台及长时流畅度仍未解决。

## 2026-10-19 跨电脑固定检查点（原始 0.2.6 历史）

**最新入口是仓库根目录 `HANDOFF.md`。** 当前已刷应用为 0.2.6，构建源码 `e84fbc8dc078d966cdeddce544dfc0418f6c7c76`，准确镜像及调试符号在 `checkpoints/radio-dev-0.2.6/`。用户确认有声音但有间歇停顿，优化尚未结束；当前不发布正式 Release。

Claude 的原设计、美术、全部收到的图片、动画源视频、原型、完整源字体和台表已并入 `design/nabo-radio/`，资源清单在 `handoff/resources-2026-10-19.json`。克隆后先执行 `python tools/verify_handoff.py`。设备备份、配网及原始串口日志只留原电脑。

后续已修：自适应小块压缩环（每块 1 KiB）、网络栈提前保留、界面像素/显示列表分开、界面先于播放分配、退出电台时先释放场景再建原设置、USB 状态查询移出扬声器线程；新增 `FM-Audio` 定量诊断。内存余量和 HTTP 超时仍需继续定位，音质与实体键未验收。安全刷写的兼容 bootloader 差异、源码导航和新电脑步骤均见根交接本。

下方为此前软件检查点及更早历史，不能覆盖上面的最新状态。

## 2026-10-19 Codex 接续检查点

从 Claude 的 `ca25669` bundle 恢复到独立电台仓库；原设计现在也归档于 `design/nabo-radio/`。以下记录的是当时尚未刷机的初始软件检查点。

构建源码 `3d441b50ef32ee6912133aaf2964fa68db43d68c`（分支 `codex/fm-integration`）。应用 3478912 B，资源 3019577 B；余量分别 649856 B / 126151 B。SHA256 与验证边界见 `validation/radio-dev-2026-10-19.json`。

- `radio_assets/`：NABO 13 动作 247 帧、1,105 台 NFC1 台表、文楷 23 px / 思源黑体 12 px 子集、字标、时钟和道具；输出记录在 `radio-assets.json`。
- `nabo_radio_sprites.c`：有界解析 NFM1；第零帧清空、顺序差分、随机跳帧重放；宿主与 Python 解码逐像素一致。
- `nabo_radio_gfx.c` / `nabo_radio_view.cc`：单条 8 行 DMA 缓冲、条带哈希跳过未变画面；索引缓存约 22 KiB，仅电台页存在；保留 `panel_borrowed`。
- `nabo_radio_keys.c`：250 ms 双击、650 ms 长按、下键加速重复、上键先动后撤回。列表与播放器位置分开；夜间第一次键只唤醒。
- `nabo_radio.c`：32 KiB 压缩环，网络任务与扬声器所有者分离；12 KiB 开播；断线保留数据并在网络任务重连。MP3 经半带 DSP；ADTS AAC 调用 Espressif 解码器，超出固定 PCM 上限的格式失败退出；AAC/HE-AAC 尚未真机测试。
- ES8311 已关闭设备的采样率白名单补齐 8/11.025/12/16/22.05/24 kHz。回小智等待解码任务及 GUI 归还内存，再恢复 TLS/Opus。
- 收藏最多 128 个稳定 UID，最近十台也是 UID；新 NVS 键 `favorites2/recent2/station_uid/local/loudness/noise/autoplay`，保留旧键。旧 47 台按 URL 中的台号优先、台名次之迁移收藏和当前台，避免套用新排序。
- 注册十项 `self.radio.*`；小智主页可搜索后打开电台。仍暂停语音运行电台，边播边语音/20% 压低未实现。
- 三项新宿主脚本直接调用生产实现；`docs/images/radio/` 为宿主绘制，不是工牌截图。50 次开关是宿主资源释放检查，不是真机切台。

下一步：

1. 核对连接工牌 MAC、布局和完整备份，再安装候选 app + assets；构建不是设备验收。
2. 国内 Wi-Fi 测 MP3 44.1/48/32/22.05 kHz 和港新马 AAC，记录实际堆、最大块、栈。空闲堆 60 KiB 尚未验证，32 KiB 环可能需调整。
3. 实体键、夜间唤醒、50 次切台、1 小时连播、退出后的连续小智对话；附 app/assets SHA256。
4. 完成在线台表（需要国内检测源）、IP 地区、通用字库缺字回退、语音与播放并行。目前地区可手动设定。
5. HLS/ICY/play_url/队列、离线助眠声、DLNA 仍是后续分期。设计目录原型仍只用 S1/S2，与固件动画分开记录。
6. GitHub 新仓库尚未创建/推送；当前是本地开发检查点，不能将未上机候选发布为正式 Release。

下方旧记录仅适用于各自版本，不代表本次电台验收。

## 当前交接状态（NABO FM 电台版，开发中）

本仓库从牌桌版 v1.6.0（`2de454a`）分出，目标是《NABO FM》电台版。设计、美术规格、电台表和交互原型在设计仓库 `nabo-radio/`（`DESIGN.md`、`ART_BRIEF.md`、`catalog/`、`prototype/`）。

已完成（`2008f4e`）：
- 删除牌桌、谁是卧底、跳一跳及其素材、测试和生成器；`nabo_dice_render.c` 随牌桌一起删除。
- 从 `2b59ce8^` 取回 v1.2 电台：`nabo_radio*.c/h`、Helix MP3（`helix/`，RPSL/RCSL 许可原样保留）、`nabo_helix_buffers.c` 及其宿主测试。作为可编译起点，界面暂用旧的文字菜单。
- 工具菜单：返回 NABO / 网络电台 / 更多设置；语音“打开电台 / 网络电台 / 收音机”，MCP `self.apps.open` 的 `radio`。
- 新增 `nabo_radio_dsp.c`（输出级：半带 2:1 抽取、响度均衡、淡入淡出、限幅），14 项宿主检查，尚未接入 worker。
- 宿主测试全部通过（`python tools/run_tests.py`）。
- **ESP-IDF 6.0.2 编译通过**（云端环境，依赖组件取自牌桌版工程的 managed_components）：应用 3,334,112 字节，应用分区剩 0xc2020（776 KB，19%）；`generated_assets.bin` 1,324,917 字节，资源分区剩约 1.82 MB。

尚未完成：新电台界面（直绘，参照牌桌的显示列表 + 脏矩形）、NABO DJ 精灵、新音频链路（压缩数据缓冲、按台采样率 2:1 半带抽取、淡入淡出、限幅、响度均衡）、`esp_audio_codec` MP3/AAC、电台表放进资源分区、设备侧 MCP 电台工具。**尚未上机验证。**

实测要点（2026-10-19，用户网络）：蜻蜓 64k 流为 MP3 立体声，采样率 44.1 / 48 / 32 kHz 混合；流开头落在帧中间；`lhttp.qtfm.cn` 允许跨域读取，可在浏览器里做帧头校验。

---

以下为牌桌版的交接记录，涉及显示争用、内存和音频所有权的修复对电台版同样适用。


## 当前交接状态

当前版本为 **v1.6.0 谁是卧底版**：复用现成实机验证应用，3,975,984 字节，SHA256 `de9ff6def9c9e49464d77607aca6a53d4e162f8da387dc2e0f379106101d7327`。资源 SHA256 `b7230904f977912bdb4b31b21bb47ad105c7e49e4a2f7a162664ba8c43107785`，bootloader、分区表和资源与 v1.5.0 相同。新增 `nabo_table_uc.c`（3–8 人规则）、`nabo_table_scene_uc.c`（画面）和 `nabo_uc_words.c/.h`（264 组词库），原型同步至 `game-art/table/prototype/undercover.js`；词库来源为 `game-art/table/undercover/words.txt`，生成器 `tools/generate_uc_words.py`。本版删除节奏回声固件与相关测试，勿将历史文档中的节奏入口当作本版功能。

**屏幕争用修复：** `NaboDisplay::BeginDirectDraw()` 设置 `panel_borrowed`；LVGL flush 在牌桌持有面板期间只回报完成。仅暂停 LVGL 刷新定时器不够，因为配网状态变更会重新唤醒刷新。不要移除此保护。实机 5/8 人完整流程及 10 轮牌桌进出压力测试已通过，验证范围见 `docs/VALIDATION.md` 和 `validation/v1.6.0.json`；现成镜像与日后干净构建必须分别记录，不能静默替换。

上一版 Release 为 **v1.5.0 牌桌多人版**（本仓库），应用 SHA256 `849a1ce6468c9f927ba94e951f74c5dbf1a9c1ec0da06bbdc8d63187e1f43e05`：牌桌支持 2–4 人（21 点并列最高都得分，大话骰改为罚分制），应用分区剩约 337 KB，bootloader、分区表沿用 v1.4.0。上一版 **v1.4.0 牌桌版**，应用 SHA256 `b9502231ad4d8b2056e91c567858826fbc91c30fa25dcdb60024c08892e97f4b`：新增 NABO 牌桌（21 点、大话骰），移除深潜、摇骰子、功德木鱼和蓝牙电脑快捷键（含 `nabo_pc.c`）；小智没联网时也能打开本地程序。应用分区剩约 340 KB。**资源分区随之改变**（多了 1.18 MB 的 `table_sprites.bin`，共 2.54 MB），所以发行包不再复用 v1.0.0 的资源镜像，upgrade 模式写应用和资源。牌桌的结构与注意事项见下文“NABO 牌桌”一节。

v1.3.x 深潜版：应用 4,107,808 字节，SHA256 `7cd8589aff5aa7eacf66bdff93a9cd5accc19613aa9add2ce80504507ac0a45a`（v1.3.0）；新增深潜，移除网络电台，在 [ai-passport-nabo-xiaozhi-dive](https://github.com/Liutupi/ai-passport-nabo-xiaozhi-dive) 维护。

以下为 v1.2.0 及更早的交接状态。

最新 Release 为 **v1.2.0**，应用 4,053,008 字节，SHA256 `2c05a52efc76a38597c0a632082c913f8af0acaf6d357f889a58b92829a3c984`：海报风菜单（`nabo_menu_view.cc`）与思源黑体 4-bit 字体。应用分区只剩约 75 KB，新增大块资源前先评估字体子集或改用压缩字体。截图脚本测试只发方向键，不要在设置页发 OK（会改用户设置）。

v1.1.0：应用 3,945,968 字节，SHA256 `5c0d5117bc69ee0fd7115e0a73ce2c12df6e409ccd3a6e08ebe134ef39394620`。新增节奏回声、菜单重设计和菜单面板按需分配；验证范围见 [VALIDATION.md](VALIDATION.md)。发行包的 bootloader、分区表和资源镜像直接复用 v1.0.0 Release（与实机逐字节一致），只替换应用：本机重新构建的 bootloader/资源与已部署字节不同，若把它们打进包，`flash_release.py --mode upgrade` 会对现有设备拒绝写入。

下面是 v1.0.0 的原始交接状态。

首个定制 Release 为 **v1.0.0**，应用 3,901,088 字节，SHA256：

```text
b18f915d0295181609b225b475511e1e8a5e60d950b2390fc33191e7c0278d40
```

这是已刷入并获得“摇骰子、木鱼、网络电台三个都能打开”反馈的镜像。此前连续对话内存修正版获得“连续对话正常”；连续蓄力和音量获得“连跳和音量都合适”。各项证据对应的镜像与限制见 [VALIDATION.md](VALIDATION.md)，不能把此前全部测试说成对最新镜像又完整执行了一遍。

## 代码职责

目录前缀 `xiaozhi-esp32/main/boards/folotoy/ai-passport-nabo/`：

| 文件 | 维护职责 |
|---|---|
| ai_passport_board.cc / config.h | 硬件初始化、ADC 按键、双击/长按、语音回调；引脚保持原工牌布局 |
| nabo_display.cc / muse_pixel.c | 224×224 NABO 动画和流式行解码，避免整帧内存分配 |
| nabo_apps.cc | 工具菜单、页面状态、导航、任务/协议所有权、音频切换、设备侧 MCP |
| nabo_voice_commands.c | 有界无堆分配的明确语音命令解析；拒绝否定、询问、多程序歧义 |
| nabo_jump_model.c / nabo_jump_rig.c | 游戏物理、平台和人体动作；保留落地/镜头期间的输入 |
| nabo_jump_sound.c / jump_sounds.bin | PCM 音效、蓄力渐变与切换过渡 |
| nabo_table.c | 牌桌任务（30 fps esp_timer 节拍）、原始按键沿（UP/DOWN 连发、OK 松开生效、长按 2 秒暂停）、NVS 抢局与人数设置（`table_target` / `table_players`）、`>table.*` 串口诊断 |
| nabo_table_model.c / nabo_table_bid.c | 牌桌规则（2–4 人）：封面、交接、暂停、21 点、大话骰（罚分制）；纯逻辑，与浏览器原型逐步对拍（test_table.py） |
| nabo_table_gfx.c / shapes.c / scene*.c | 显示列表 + 脏矩形，8 行条带合成；绒布、牌、骰盅、文字、精灵；只用整数 |
| nabo_table_sprites.c | 从资源分区原地读取 table_sprites.bin，大图逐行解码、小图关键帧+差分 |
| nabo_table_sound.c | 现场合成：骰子共振器（复用 nabo_dice_render.c）、滤波噪声纸牌声、铃声 |
| nabo_dice_render.c | 立体骰子渲染与敲击声；摇骰子程序已删，牌桌用 nabo_dice_pose / nabo_dice_die_row |
| nabo_rhythm_model.c / nabo_rhythm_sound.c | 节奏回声关卡、判定（16 kHz 采样时钟）、合成音色；宿主测试 test_rhythm.py |
| nabo_rhythm_play.c | 音频 worker 与 LVGL 之间的歌曲时钟锚点、音效队列、延迟补偿 |
| nabo_rhythm_ui.c / nabo_rhythm_stage.c | 选关/结算与演奏画面；对象只在程序打开时存在 |
| nabo_*_font_*.c | 由 tools/generate_fonts.py 生成；新增中文界面文字后重新生成，字形不足会显示缺字 |

核心层只通过 `Board::HandleRecognizedSpeech()` 可选接口调用板型，不能在 `application.cc` 中硬编码 NABO 程序关键词。STT 回调通过 `Application::Schedule()` 在主任务显示文字并处理打开指令。其他板型默认不处理该接口。

## 不要退回的修复

**小智内存：** C3 没有 PSRAM。旧 ES8311 整帧双声道临时 vector 和过长队列造成 `std::bad_alloc`，包括长回答后的启动提示循环崩溃。现在用 `stereo_frames.h` 每次 160 帧、640 字节栈缓冲转换；NABO 的 PCM 队列 1 帧，压缩收发缓冲 600 ms。保持 Opus **24 KiB 栈**：真实语音曾使用近 20 KiB，不能根据待机栈水位随意缩小。其他板型的队列默认不变。

**程序切换：** 本地程序持有音频时停止小智，返回前完成本地音频退出；主任务回收已完成的协议初始化任务后再恢复 AudioService，不能叠加约 8 KiB 的已完成任务栈。避免在 LVGL 显示锁内阻塞等待音频任务。

**BLE：** 电脑快捷键与 `nabo_pc.c` 已在 v1.4.0 删除，固件不再启动蓝牙。若将来恢复：先断开、停止 host、清空 GATT 属性，再 NimBLE deinit/释放内存，并用真实已配对电脑验证退出。

**游戏输入与画面：** 落地、镜头移动甚至飞行末段可缓冲下一次蓄力；保留第一次按下时间和提前松开的时长。不能等画面停止才接受按键。失败动作在平台旁结束，不让 NABO 整体飞出屏幕。已把绘制范围缩到有效扫描行和角色/阴影局部区域；避免恢复逐像素全场景检查。

**声音：** 本地程序（牌桌、跳一跳、节奏回声）用 16 kHz 双声道 I2S、4×160 帧 DMA。小智 24 kHz 与本地音效的所有权和采样率不同，要先完成退出再切换。蓄力 gain 0.65、15 ms 淡入淡出、约 8 ms 声音切换过渡已被接受。界面 100% 需保留扬声器余量，不能直接恢复满幅。

**按键：** 右侧上键双击回上一级，单击原功能、长按直接回小智。页面父级要保留，特别是设置编辑、状态页、电脑分组。左侧电源键不是可读取的 MCU 按键。

**菜单内存：** LVGL 使用系统堆（`CONFIG_LV_USE_CLIB_MALLOC`）。菜单面板在进入菜单页时创建、回到 NABO 或进入游戏时删除，节奏回声同理。不要改回开机常驻：v1.1.0 首次常驻版本在开机 MQTT 发布工具列表时出现 `esp-aes: Failed to allocate memory`，改为按需后待机空闲堆约 30 KB → 36 KB。

**节奏判定：** 判定以 OK/下键的 press-down 时间戳为准（下键按下由板型 `OnPressDown` 转发），通过 worker 写 I2S 前记录的锚点换算到歌曲采样位置。默认补偿 42 ms，可用 `>rhythm.latency=` 调整；`>rhythm.best.reset` 清除成绩，脚本测试后应执行，避免留下假成绩。

## NABO 牌桌

**屏幕所有权：** 与此前的深潜相同，不经过 LVGL。`change_page` 在 LVGL 任务里 `BeginDirectDraw()` 后调用 `nabo_table_open()`；离开时 `nabo_table_close()` 等最后一个条带发完再 `EndDirectDraw()`。

**按键：** 牌桌直接读三个键的按下/松开（板型里 UP 新增了 `OnPressDown/OnPressUp` → `nabo_apps_up_edge`）。对局中屏蔽双击上键（返回上一级）和长按上键（回小智），只有牌桌封面上才生效，避免按住上键翻叫数时被踢出去。中途退出走暂停菜单。

**内存：** 打开时分配显示列表等约 18 KB（多人版把显示列表、骰子和扑克牌上限加大，原来约 12 KB）、两条 3.8 KB 条带、5.3 KB 精灵缓冲、6 KB 任务栈。实测打开后空闲堆约 65 KB（配网模式下），最大连续块约 34 KB。大图帧直接从 Flash 解码，不占内存。

**多人：** 人数在开局时固定到 `nt_t.np`，所有按玩家循环的地方都只到 `np`。规则改动（并列都得分、罚分制、加赛）见 `game-art/table/DESIGN.md` 第 9 节。改规则时**先改浏览器原型**，再用 `tests/table_host/golden_server.py` + `record_golden.js` 重新录对拍轨迹（Playwright 打开原型页面后注入脚本，调用 `recordTrace`），C 模型必须逐步一致。渲染器每一帧超出显示列表、骰子、扑克牌或字形池上限时会置 `ntg_t.overflow`，新画面要用四人六骰的轨迹检查一遍。

**开机即玩：** `application.cc`（NABO 板型分支）允许在 starting / wifi_configuring 状态 `BeginLocalApp()`，保持开机状态不变；期间的联网事件只记录下来，`EndLocalApp()` 时继续开机流程（若已联网就补发一次联网事件去激活）。已在没有可用 Wi-Fi 的工牌上验证；"游戏中途联网、退出后激活"尚未在真机验证。

**素材流程：** `game-art/table/`：起始图 → 图生视频（`art/video/`）→ `game-art/table/tools/clip_to_sprites.py` 抽帧抠图 → `tools/generate_table_sprites.py` 打包。改了素材要重新构建资源镜像，并用 upgrade（写应用+资源）刷写。

**画面速度：** 平时每帧 1.5–6.5 ms，28–30 fps；换画面时整屏重画 55–180 ms，会有一下停顿，后续可优化。不要把渲染代码放进 IRAM（深潜试过，开机即 abort 循环）。

## 诊断与回归

开发固件保留串口命令，如 `>apps.status`、`>apps.open=home`、`>apps.open=jump`。它们用于定位页面和资源状态；使用实体按键/真实语音做最终验收。游戏诊断必须打开测试抑制模式，避免自动操作刷新用户最高分。

主页 `VoiceMemory` 每约 5 秒打印 heap、largest、Opus 栈余量和音频计数，只在主页工作，不干扰游戏绘制。记录长回答、打断、返回主页后再对话，检查 abort/assert、栈和最大连续内存块。日志可能包含 ASR/TTS、网络信息，只保存到被 Git 忽略的 `private/`，公开问题仅用脱敏堆栈和统计。

按影响运行测试：语音解析/PCM/游戏（跳一跳、节奏回声、牌桌）生产 C 测试，以及上游宿主测试。修改音频/生命周期必须加真实连续对话与程序来回切换；修改导航需验证所有父页面。不要反复运行不相关测试替代硬件验证。

## 下一版发布流程

1. 从本仓库的 `v1.0.0` 标签开始，保持板型身份 `ai-passport-nabo` 和分区接口。
2. 读取源码中的 AGENTS.md，修改最窄职责层，保留第三方许可。改动编译配置时更新锁定/配置并记录理由。
3. 宿主测试、目标构建、相应实机回归；给每份设备验证摘要记录具体应用 SHA256。
4. 打包四个**构建输出镜像**，从不打包已配网设备的整片 Flash、NVS 或 private。upgrade 模式写应用和资源（牌桌素材在资源分区）。
5. 更新 CHANGELOG、交接、验证边界、manifest 和 SHA256SUMS；标签与源码提交绑定。
6. 创建 Release 后下载公开资产重新核对 SHA256 和 ZIP 成员，确认文档/许可证随包。

## 已知边界

自定义单应用分区不能使用标准 OTA。网络语音需要小智服务。仅此 ESP32-C3 工牌经实机验证，其他上游板型保留源码但不是本项目的兼容承诺。首版不保证任意网络环境永久无重连，也不承诺全阶段 60 FPS；用户实际接受的连跳和声音、测量范围均在验证记录中。
