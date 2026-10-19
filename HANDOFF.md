# NABO FM 跨电脑交接 — 2026-10-19

先读本文件，再读 `docs/HANDOFF.md`、`AGENTS.md` 和 `xiaozhi-esp32/AGENTS.md`。用户要求先固定源码和全部素材，在另一台电脑继续优化；此检查点没有完成音质验收，不能当作正式固件发布。

## 当前准确状态

- 最新已构建、刷入的应用：开发版 **0.2.6**，源码提交 **`e84fbc8dc078d966cdeddce544dfc0418f6c7c76`**。
- 应用 3,478,512 B，SHA256 `a948b6e8ee75a1a4102373f7338f49658e32e330ef2841d73ca479975791abc5`。
- 资源 3,019,577 B，SHA256 `0de5c9d3b8f824730b169549463b06d00bbce27814daf1d633ac3b6a068dbb3f`。
- `checkpoints/radio-dev-0.2.6/` 保存四个构建镜像、manifest、**对应 ELF、map、实际构建 sdkconfig**。这些是现成构建输出；不要用新电脑重编译出的不同二进制冒充这个检查点。
- 当前工牌已保留原启动程序、分区、NVS、PHY、files 和已配置的 Wi-Fi。最近几次修复只写应用，并逐字节验证全部保留区。音量 100、亮度 75、休眠 120 秒在刷后状态中仍保留。
- 用户真实反馈：**“有声音，但卡顿或异常” → “断断续续，中间有停顿”**。这个问题尚未验收通过。
- 0.2.5 曾看到两个台实际解码，48 kHz 立体声转 24 kHz；不能据此说 1,105 台都可播。0.2.6 最后短时观察没有 panic，但所选第 16–18 项出现 HTTP 连接超时，不能把“已刷入”写成“音频正常”。
- 没有正式 GitHub Release。仓库的检查点标签只用于固定开发成果。

## 新电脑拿到全部数据

```sh
gh repo clone Liutupi/ai-passport-nabo-xiaozhi-radio
cd ai-passport-nabo-xiaozhi-radio
python tools/verify_handoff.py
```

仓库为私有，需要登录有访问权限的 GitHub 账号。资源直接存于普通 Git，不依赖 Git LFS 或本机外部素材路径。

| 目录 | 内容 |
|---|---|
| `design/nabo-radio/` | Claude 原设计目录快照：设计说明、美术规格、原交接本、原型、台表和制作脚本 |
| `design/nabo-radio/art/` | 全部收到的图片、参考、道具、调色板、14 段动画源视频、定稿交付包、精灵包和预览 |
| `design/nabo-radio/prototype/` | 可离线打开的浏览器原型，含 5 个 WOFF2 字体和内嵌精灵数据 |
| `design/nabo-radio/fonts/` | 本次实际使用的文楷 TTF、思源黑体 OTF 及原始 OFL；已与资源报告的源字体 SHA256 对照 |
| `xiaozhi-esp32/main/boards/folotoy/ai-passport-nabo/radio_assets/` | 固件实际使用的动画、字体、台表和道具二进制 |
| `docs/images/radio/` | 生产绘制代码的宿主预览，**不是工牌截图** |
| `checkpoints/radio-dev-0.2.6/` | 精确的开发固件与调试符号 |
| `handoff/resources-2026-10-19.json` | 全部交接资源的文件大小和 SHA256 清单 |

原设计目录里的 **92 个图片文件全部复制并逐一核对**。收到的目录没有原始抽帧 `.npz` 文件；源视频、抽帧脚本和固件使用的完整 247 帧精灵包均已保存，正常编译不需要重新抽帧。

没有上传工牌 Flash 备份、NVS、Wi-Fi 凭据、设备原始日志、云端上传回执或 `private` 下的依赖缓存压缩包。ESP-IDF 和 managed_components 按锁定依赖正常安装/下载。

## 构建、测试、看原型

使用 ESP-IDF **6.0.2**、目标 ESP32-C3、8 MB、无 PSRAM；Python 3.11。先安装并初始化 IDF 环境，再在仓库根目录执行：

```sh
python -m pip install -r requirements-dev.txt
python tools/build.py
python tools/run_tests.py
```

宿主测试需要 GCC/G++ 在 PATH。Windows 推荐 w64devkit，PowerShell 设置 `$env:CXX='g++'`，避免反斜杠路径被 shlex 解释。测试不打开串口、不刷机。新电脑若需重做资源：

```sh
python tools/pack_fm_assets.py --design design/nabo-radio --wenkai design/nabo-radio/fonts/LXGWWenKai-Regular.ttf --sans design/nabo-radio/fonts/SourceHanSansSC-Normal.otf
```

正常构建直接使用已提交的二进制资源，不必先运行上面的重做命令。制作原型及本地预览：

```sh
python design/nabo-radio/prototype/build.py
python -m http.server 8000 --directory design/nabo-radio/prototype
```

浏览器打开 `http://localhost:8000`。原型仍是设计交互参考，不代表固件已实现全部功能。

## 刷机边界：必须先读

当前设备最初装的是离线 NABO 游戏合集，没有 Wi-Fi 记录；用户明确同意刷这块、之后配网，随后已联网。原电脑串口名不是新电脑的设备身份；**重新枚举串口并读取 live MAC**，不能套用旧端口或猜 MAC。

| 分区 | 偏移 / 大小 |
|---|---|
| bootloader / partition table | `0x0` / `0x8000` |
| NVS | `0x9000` / `0x6000` |
| PHY | `0xf000` / `0x1000` |
| factory app | `0x10000` / `0x3f0000` |
| assets | `0x400000` / `0x300000` |
| files | `0x700000` / `0x100000` |

现有工牌 bootloader 与候选包 **构建字节不同**，但分区表逐字节相同。原电脑已核对两者为有效 ESP32-C3 / IDF 6.0.2 镜像，校验和、附加摘要、芯片版本、DIO/80 MHz/8 MB 均兼容，才定向保留原 bootloader，写 app + assets；后续仅写 app。

公开工具 `tools/flash_release.py --mode upgrade` 仍保留严格的 bootloader 字节检查，**对这块工牌会在写前拒绝**。这是已知保护边界，不是电台构建坏了。不要为绕过它执行整片擦除，或随意改为 install。后续升级应重新完整备份并校验当前设备，核对布局及兼容 bootloader，再制定仅写 app（资源改变时加写 assets）的受保护流程。检查点附带的是构建 bootloader，**不是设备私有 bootloader 备份**。

本地安全脚本含具体设备身份，未放入仓库；设备备份留在原电脑，不应跨设备使用。

## 代码入口与已做内容

板型目录：`xiaozhi-esp32/main/boards/folotoy/ai-passport-nabo/`。

- `nabo_apps.cc`：原设置/NVS、串口诊断、本地音频所有权与 I2S 写入；`>apps.open=radio`、`>apps.key=up/down/ok`、`>apps.status` 可做诊断。串口注入不等于实体按键验收。
- `nabo_radio.c`：网络生产任务、分块压缩环、Helix/AAC、DSP、收藏/最近 UID、重连和释放。环容量按实际内存选 32/16/8 KiB，**每块 1 KiB**，网络栈在码流和缓冲前保留；预缓冲不超过容量的 3/4。
- `nabo_radio_view.cc` / `nabo_radio_gfx.c`：午夜琥珀 240×320，8 行 DMA 条带、脏条带哈希；动画像素和绘制列表拆分分配；先建界面再启动播放。切到原设置页先释放 FM 场景，避免 LVGL 缺内存崩溃。
- `nabo_radio_sprites.c`：NFM1 13 动作/247 帧，顺序差分与随机跳帧；`nabo_radio_catalog.c`：NFC1 1,105 台。
- `nabo_radio_keys.c`：250 ms 双击、650 ms 长按、下键加速、上键先动后撤回。**枚举顺序是 UP / OK / DOWN**，不要按 UP / DOWN / OK 猜数字。
- `nabo_radio_dsp.c`：半带降采样、响度均衡、高通、限幅、淡入与暂停收尾。
- `nabo_apps.cc` 的 `nabo_apps_register_tools()`：十项 `self.radio.*` 工具。当前播放会暂停小智语音，边播边语音/20% ducking 尚未实现。
- `main/audio/codecs/es8311_audio_codec.cc`：允许 8/11.025/12/16/22.05/24 kHz，变采样率须在双向设备关闭时进行。

## 最优先继续的问题

1. **卡顿尚未解决。** 0.2.6 已把 `>apps.status` 的 GUI 锁和格式化移出扬声器线程，防止诊断等待整帧绘制；尚未得到用户对这版流畅度的确认。
2. **内存仍紧。** 曾在 16 KiB 环运行时只剩约 4.9–8.6 KiB 空闲、最大块约 1.25 KiB，同时 HTTP 超时。分配成功不代表给网络留够空间；需继续实测环容量、HTTP/TCP 和 codec 额外分配。
3. **看实际供音指标。** `FM-Audio` 每约 5 秒打印 `rate/out/decoded/short/max_decode_us/max_loop_us/min_buffered/stack`。`out` 包括零填充和调台声，不能单独当作音乐正常；`decoded` 是 read 返回样本，切台收尾也可能计入。结合环余量、HTTP 日志、墙钟和听感定位网络饥饿、解码或 I2S 供音间隔。
4. 0.2.5 曾在播放中切回原菜单时发生 LVGL `lv_obj_class_create_obj` 空指针崩溃（内存不足）。0.2.6 已释放 FM 场景再建菜单，**仍需反复进出设置/电台和重启回归**。
5. AAC/HE-AAC、全部台源、1 小时连播、50 次实机切台、夜间唤醒、实体键手感、退出后的连续小智对话均未完成验证。

不要继续把所有失败统称为“这个台暂时连不上”：新增分阶段诊断可区分 workspace、network task、compressed buffer 与 HTTP 状态。保留这些诊断，先收集定量数据再改参数。

## 已有验证及不能混淆的结果

- ESP-IDF 6.0.2 的当前目标构建成功，写后应用与全部保护区校验通过。
- 完整宿主回归在最初集成检查点通过；后续受影响的生产音频/场景测试随修改重跑。
- 分块环：8/16/32 KiB 输出 54,720 样本在 5 种读取粒度一致；3 MiB 双线程传输；32 位位置回绕；部分分配失败清理；EOF/暂停尾音；50 次宿主释放重开。
- 场景：1,105 台、菜单/列表/搜索/时钟、延迟切台、收藏双击、长按、上级返回和唤醒抑制。
- 这些测试通过 **不代表声音、实体键、网络或长时稳定性已验收**。

设计保持用户已定的 NABO 形象、午夜琥珀、文楷台名和思源黑体小字。新电脑应从这个检查点继续修，不要重新运行早期移植生成器覆盖当前实现。
