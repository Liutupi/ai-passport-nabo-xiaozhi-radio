# NABO FM 电台版 · 交接本（2026-10-19 16:00，Claude → Codex）

先读：本文件 → `DESIGN.md`（功能/界面/交互/音质方案）→ `ART_BRIEF.md`（美术规格，第 8 节为定稿配色）→ 固件仓库 `docs/HANDOFF.md` 顶部。
用户：刘东坡（GitHub Liutupi）。目标：面向玩法社区所有人的“最好的电台”，小智固件里只做电台 + 设置。

## 1. 已定的决定（不要再改，除非用户说）

| 项 | 决定 |
|---|---|
| 版本 | 新仓库 `ai-passport-nabo-xiaozhi-radio`，从牌桌版 v1.6.0 分出；删除牌桌、谁是卧底、跳一跳；保留设置页 |
| 外观 | **午夜琥珀配色 + 霞鹜文楷（台名、NABO FM 字标）+ 思源黑体（界面小字）**。色表 `art/palette/radio_final.json`（ON AIR/指针用纯红 #FF3B30，耳机珊瑚红 #E97967） |
| 按键 | ▼单击下一台、按住▼快速扫台；▲单击上一台；双击▲返回、长按▲回小智（全局约定）；OK 单击播放/暂停、**OK 双击收藏、OK 长按菜单**（列表/拼音搜台/定时/沙沙声/设置）。停手 500 ms 才真正连接 |
| ▲单击冲突 | 用户说手感“还可以”，原型默认方案 A（先动、构成双击再撤回）；未正式拍板 |
| NABO 尺寸 | 帽顶到桌沿 **124 px**（不是 128，戴耳机头梁会顶出舞台）；精灵 158×139，屏幕坐标左上 (41,11)，桌沿线 y=150 |
| 音频 | 输出 = 码流采样率的一半（48k→24k、44.1k→22.05k、32k→16k；≤24k 直通），半带 FIR 2:1 抽取；先播缓冲里的压缩数据，断线续播；响度均衡 |
| 网易云 | 不内置任何音乐平台。第二期做通用 `self.radio.play_url` 设备工具，用户自己在 xiaozhi.me 接音乐 MCP（用户主要用 iPhone，DLNA 只做安卓附加功能） |
| 在线电台表 | 用户同意放 GitHub 公开仓库每周更新；**注意：蜻蜓可能挡海外 IP，GitHub Actions 实测要在国内网络**（见 6.3） |

## 2. 文件在哪里（用户电脑 `D:\AI PASSPORT XIAOZHI\`）

```
nabo-radio/                       设计仓库（还不是 git 仓库，建议 git init 后推到 GitHub）
├── HANDOFF.md                    本文件
├── DESIGN.md / ART_BRIEF.md / CODEX_PROMPTS.md
├── catalog/
│   ├── stations.json             1,105 台统一电台表（字段 qt/name/url/codec/region/types/heat/py/health/sources）
│   ├── health_2026-10-19.json    实测结果：1,073 通过，37 个 404 已移除，采样率分布、首字节延迟
│   └── sources/                  蜻蜓抓取 TSV、魏宝辉/小海马/旧版来源 JSON + README
├── tools/
│   ├── crawl_qingting.js         在国内浏览器控制台运行，抓蜻蜓公开电台页（同源）
│   ├── import_sources.py         解析三个开源来源
│   ├── build_catalog.py          合并去重 + 体检结果 → stations.json 和 prototype/data.json
│   ├── clip_to_frames.py         Grok 绿幕视频 → 抠图去溢色 → 158×139 RGBA 帧（.npz）
│   └── pack_radio_sprites.py     帧 → radio_sprites.bin（格式见文件头注释），自带逐帧解码校验
├── prototype/                    浏览器原型（index.html 由 build.py 把 template.html + data.json + 字体 + 精灵拼成）
├── art/
│   ├── design/ props/ palette/ preview/ delivery/   Codex 三轮美术（已验收）
│   ├── video/batch1/source/       Grok 13 条视频 + PICKS.md（phones_on 用 v2）
│   └── sprites/radio_sprites.bin  ★ 已打包的 NABO DJ 动画（1,316 KB），report.json，sheet.png
└── private/mc.tgz                 ESP-IDF 依赖组件包（managed_components，来自牌桌版工程，不要提交）

radio.bundle.part00 + part01 + join_radio_bundle.bat   固件仓库 git bundle（双击 bat 合并并 clone）
```

原型在线页（用户的 claude.ai artifact）：NABO FM 交互原型，默认主题“定稿”。

## 3. 固件仓库 `ai-passport-nabo-xiaozhi-radio`（bundle 内，分支 main）

提交记录：
- `2008f4e` 从牌桌版 v1.6.0 分出；删牌桌/卧底/跳一跳；从 `2b59ce8^` 取回 v1.2 电台（`nabo_radio*.c`、Helix MP3、`nabo_helix_buffers.c` 及测试）；工具菜单 = 返回 NABO / 网络电台 / 更多设置；语音“打开电台/网络电台/收音机”；MCP `self.apps.open` 的 `radio`
- `89d8449` / `ca25669` 文档
- `924dec6` + `999eaf7` 新增 `nabo_radio_dsp.c/.h`（输出级：立体声混单声道 → 31 阶半带 FIR 2:1 → 120 Hz 高通 → 慢速响度均衡（不放大静音）→ 淡入淡出 → -2 dB 余量 → 软限幅；≤24 kHz 直通）+ `test_radio_dsp.py` 14 项。**尚未接入 worker**

状态：
- `python tools/run_tests.py` 全部通过（语音 55、PCM、电台 DSP 14、Helix 分配器、电台流/解码/菜单 48）
- **ESP-IDF 6.0.2 编译通过**：应用 3,334,112 B，app 分区剩 776 KB；`generated_assets.bin` 1,324,917 B（其中 1,257 KB 是小智对话字幕用的 `font_noto_sans_common_20_4.bin`，不能删），资源分区剩约 1.82 MB
- **从未上机**。当前界面还是 v1.2 的文字菜单

编译环境提示：乐鑫组件站和 dl.espressif.com 在云端被拦时，用 `private/mc.tgz` 解到 `xiaozhi-esp32/managed_components`；用户 Windows 本机已有 IDF 6.0.2，可直接 `python tools/build.py`。

## 4. 下一步（按顺序）

1. **把仓库推上 GitHub**：用户新建 `Liutupi/ai-passport-nabo-xiaozhi-radio`，clone bundle 后 push。设计仓库 `nabo-radio` 建议也建库（`private/` 加进 .gitignore）。
2. **资源分区接入动画**：`radio_sprites.bin` 放到板型目录 `radio_assets/`，CMake 里 `set(DEFAULT_ASSETS_EXTRA_FILES ".../radio_assets")`（牌桌版用法，已删，参照 git 历史 2de454a 的 CMakeLists 第 830 行），运行时 `Assets::GetAssetData("radio_sprites.bin")` 原地读取。发布时 upgrade 要同时写 app + assets。
3. **精灵解码器 `nabo_radio_sprites.c`**：格式见 `tools/pack_radio_sprites.py` 文件头和 `write_pack()` 注释。要点：第 0 帧为关键帧（skip=透明），之后每帧**相对上一帧**（skip=不变）；固件保留一块 158×139 字节索引缓冲（约 22 KB，仅电台页存在）；循环回到第 0 帧时重解关键帧；调色板 64×RGB565，索引 0 透明（画舞台底色/背景）。写宿主测试：用 Python 生成的 bin 逐帧比对 `pack_radio_sprites.py` 的 idxs。
4. **电台页直绘渲染**（照牌桌：`BeginDirectDraw` + 显示列表 + 脏矩形 + 8 行条带）。布局、颜色、文字、刻度、列表、菜单、搜台、夜间时钟全部以 `prototype/template.html` 为标准答案（它是能操作的规格：按键时序、500 ms 延迟连接、NABO 状态机、波段拼接、列表 7 行等）。舞台区每帧只重画 NABO 包围盒 + 电平灯 + 旋钮。
5. **NABO 状态机**（原型 `tick()` 里）：打开→wave→idle_off；播放→phones_on→tune（连接/缓冲）→音乐类台 groove/sing、其他 listen；每 60–120 s 插 sip；收藏 love；失败 oops→idle_off；暂停 phones_off→idle_off；无 Wi-Fi search；定时最后 1 分钟 doze。动作都从静止姿势开始、回到静止姿势结束，可以直接切换。
6. **字库**：用 `tools/generate_fonts.py` 的方式生成霞鹜文楷（22–23 px，覆盖 stations.json 全部台名用字 + 界面字）和思源黑体 12/13 px 子集；缺字回退到资源分区里小智的 20 px 通用字库。资源预算：现有 1.29 MB + 动画 1.32 MB + 文楷约 0.3 MB + 电台表约 0.06 MB ≈ 2.97 MB / 3.0 MB，**很紧**，文楷只收台名真正用到的字。
7. **新音频链路**：把 `nabo_radio_dsp` 接进 worker；I2S 按台切到 `nabo_radio_dsp_out_rate_for(stream_rate)`（现在固定 16 kHz，`SetDuplexSampleRate`）；改为缓冲压缩数据（32 KB ≈ 4 s，攒 1.5 s 开播）；开始/换台/暂停用 DSP 淡入淡出；断线先放完缓冲再重连。之后再评估把 Helix 换成 `esp_audio_codec`（固件已依赖 2.5，含 MP3/AAC/TS）。
8. **电台表进固件**：把 stations.json 转成紧凑二进制放资源分区；默认波段：本地（按 IP 定位城市）/收藏/最近/新闻/音乐/交通/财经/文艺/生活/体育/方言/综合/各省/港新马/网络台。
9. **设备侧 MCP 电台工具**：`self.radio.search/play/next/random/pause/resume/set_volume/now_playing/favorite/sleep_timer`，说话时电台压低到 20%。
10. **上机验证**（用户做）：按键手感、声音、连播 1 小时无卡顿、换台到出声 ≤2 s、空闲堆 ≥60 KB、来回切台 50 次无重启、退出后小智正常对话。

## 5. 动画处理记录（可复现）

```
python tools/clip_to_frames.py art/video/batch1/source <帧目录> --fps 12
python tools/pack_radio_sprites.py <帧目录> art/sprites/radio_sprites.bin --report ... --sheet ...
```
- 视频 480×480，桌沿第 420 行；缩放 124/376 → 158×158，取上 139 行；预乘 alpha 面积缩放
- 抠图：绿度 G−max(R,B) 40–150 线性；去溢色：G≤max(R,B)+8，边缘 4px 内 G≤(R+B)/2+10，黄绿像素 G≤0.85R（挥手指尖，个别帧仍有一点源视频自带的黄）
- 选段：idle_off/groove/sing 截一段从静止出发回到静止的循环；其余（含 tune/listen/doze/search 手势循环和单次动作）裁首尾静止后最多 20 帧；全部 8 fps；`SAME_TOL=30`、`ONE_SHOT_MAX=20`
- 结果 1,316 KB、13 个动作；`--sheet` 图已目视检查（帽标正向、无残绿、解码与源帧接近）
- 想更流畅：提高 ONE_SHOT_MAX / 改 12 fps 会超资源分区，需先省字库或别的资源

## 6. 注意事项 / 坑

1. **网络**：云端环境和用户电脑上的 Linux 命令行都连不上蜻蜓/电台流；用户电脑里的内置浏览器可以（国内网络）。体检脚本在浏览器里用 fetch 读前 40 KB 做帧头校验（`lhttp.qtfm.cn` 允许跨域）；StreamTheWorld 有 8 个台跨域被拦（不是坏了）；http:// 的台（香港电台 6、Astro AAC 17）浏览器测不了，要上机。
2. **蜻蜓流**：MP3 立体声，44.1k/48k/32k/22.05k 混合；开头落在帧中间（偏移几十到几百字节），必须扫帧头；首字节中位 1.07 s、P90 2.1 s。
3. **旧表错误**：旧版 `nabo_radio_stations.h` 里“中国之声”15318317 实为河北欢乐调频；以 stations.json 为准。央广频道蜻蜓网页没有，需另接（HRadio 用央广源，HLS）。
4. **竞品**（玩法社区）：城市之声 10,485 下载（自动定位、拼音搜台）、魏宝辉 345 台（响度拉齐、LAN MCP，无许可证——只用公开台名/地址，不用代码）、港新马 49 台（MIT，AAC、ICY 曲名，C3 上部分 AAC 台报 MEM_LACK）、听见（离线白噪音）。
5. 保持牌桌版留下的修复：直绘期间 `panel_borrowed`、Opus 栈 24 KiB、PCM 队列 1 帧、菜单面板按需创建、程序切换先停小智再回收协议任务（固件 `docs/HANDOFF.md`）。
6. 许可：霞鹜文楷、思源黑体、得意黑、缝合像素均 OFL；Helix RPSL/RCSL 原样保留；NABO 形象与美术不随 MIT。
7. 原型里 NABO 目前只用 S1/S2 静态图；把 `art/sprites/radio_sprites.bin` 在 JS 里解码播放是下一步（格式同固件，正好当解码器的对拍标准）。
