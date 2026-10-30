# NABO FM 下一轮优化交接 — 2026-10-30

> 作者：Claude（只读代码审阅，基于 origin/main `f66616a` = v1.0.0 发布源码，即精确候选 03）。
> **本轮没有构建、没有跑测试、没有接触工牌。** 下文所有“原因”都是基于代码的假设，必须先拿到设备日志证据再下结论。
> 接手者（Codex）：先读本文件，再按仓库惯例读根 `HANDOFF.md`、`AGENTS.md`、`xiaozhi-esp32/AGENTS.md`。

## 0. 开工前必须做

1. **本机仓库落后 2 个提交**：`D:\AI PASSPORT XIAOZHI\ai-passport-nabo-xiaozhi-radio` 停在 `ec786df`，origin/main 是 `f66616a`（v1.0.0 发布 + 社区展示）。先 `git pull --ff-only`，再开新分支（建议 `work/fm-smooth-06`）。本文件是未跟踪新文件，不会与 pull 冲突；pull 后在根 `HANDOFF.md` 顶部加一行指向本文件。
2. `.git/stale-maintenance-lock` 是 Claude 远程 `git fetch` 留下的锁文件改名，`.git/objects/9e/tmp_obj_*` 是同次残留；确认没有 git 进程后可删。
3. 本机**没有** `work/audio-candidate05-diagnostics`（04/05 实验分支，在另一台电脑，`0d98f0e`）。不要合并 05：05 实机持续播放 FAIL。新工作一律以 03（= main）为基线。
4. 设备纪律不变：每次写入都要用户明确确认具体镜像；只写应用（0x10000）；刷前备份、刷后读回全部保护区；**首帧解码 ≠ 播放成功**；不升高扬声器任务优先级（05 的优先级 11 是挂起嫌疑之一）。

## 1. 现状诊断（代码证据）

用户可感知的头号问题是“间歇停顿”。代码里的链路：

- `nabo_radio.c` 网络任务 → 压缩环（`ring_create` 按堆情况选 32/16/8 KiB）→ 扬声器所有者每 20 ms 调 `nabo_radio_read()`。
- 64 kbit/s ≈ 8 KB/s，所以环只够 **8 KiB ≈ 1 s / 16 KiB ≈ 2 s / 32 KiB ≈ 4 s** 的网络抖动。旧日志见过 16 KiB 环、空闲堆 3.9 KB。
- 环一旦读空（`decode_frame` 返回 0 且 `ring_available()==0`）就 `d->prefilled=false`，要重新攒满 `min(ring*3/4, PREFILL_BYTES=12 KiB)` 才再出声 → **每次抖动变成约 1–1.5 s 的空白**。
- 空白期间 state=BUFFERING(2)，`nabo_apps.cc` 在 `nabo_radio_noise()` 开启时会**播沙沙声**——本意是换台音效，中途重缓冲也会响，听感上就是“卡一下 + 杂音”。
- `nabo_radio_read()` 返回不足 `sample_count` 时，`nabo_apps.cc` 仍把整块（后半为 0）写进 I2S（`memset` + `output16(samples, sample_count)`），在有声→0 的边界没有淡出，可能有咔哒声。
- 网络任务读超时：`EAGAIN` 持续 8 s 才断开重连（`esp_timer_get_time() - last > 8000000`），但环最多撑 1–4 s；重连后每次都重新 `esp_http_client_init` + DNS + **TLS 握手**，再等 500 ms。
- 内存的大头是 TLS：台表 1,105 台里 **1,081 台是 https**（其中 1,051 台为 `lhttp.qtfm.cn`），只有 24 台 http。`sdkconfig.defaults` 已开 `CONFIG_MBEDTLS_DYNAMIC_BUFFER`，但握手峰值和会话仍会挤占碎片化的 C3 SRAM。环被压到 8/16 KiB、AAC 工作区分配失败（实测空闲约 54 KB、最大块约 12 KB）都和这里相关。
- 编解码分布：mp3 1,088 台，aac 17 台。

## 2. 优化项（按优先级）

### P0-A 先加遥测（纯日志，零风险，所有后续候选都依赖它）
在现有 5 秒 `FM-Audio` 日志里增加：`ring`（本次选的环大小）、`rebuffers`（中途断粮次数）、`rebuffer_ms`（累计空白时长）、`reconnects`、`open_ms`（DNS+TCP+TLS+首包耗时）、`heap_before_open / largest_before_open`、`stall_reason`（EAGAIN 超时 / read<=0 / Wi-Fi 断）。
验收：宿主测试覆盖计数器；设备上固定 3 个台各连播 10 分钟，形成 03 等效基线（遥测本身不改变播放逻辑）。

### P0-B 断粮恢复策略（小改动，直接影响听感）
- 首次连接/换台保留现在的预缓冲；**中途重缓冲**改用低水位（例如 4 KiB ≈ 0.5 s，或环的 1/4，取小），配合高/低水位滞回，不再每次重攒 12 KiB。
- 中途重缓冲**不播沙沙声**，改为 20–40 ms 淡出到静音；沙沙声只用于 CONNECTING / 换台。
- `nabo_radio_read()` 返回短块时，对已写样本尾部做几毫秒淡出（可复用 `s_last`/`s_tail` 的渐弱逻辑），避免咔哒。
- 网络停滞阈值从固定 8 s 改为约等于“环里剩余的秒数”（下限 2 s），让重连在环耗尽前开始。
验收：新增宿主测试——模拟 1 s / 3 s 网络空洞，比较 03 与新策略的静音时长和样本连续性；设备连播对比 `rebuffer_ms`。

### P0-C 蜻蜓台改用 32k 码率（同内存下缓冲秒数翻倍）
- 蜻蜓 URL 形如 `https://lhttp.qtfm.cn/live/<id>/64k.mp3`；HANDOFF 记录过对 404 台复查 `32k`/`128k` 路径，说明存在 32k 变体，但**健康台的 32k 可用率从未统计**。
- 先在电脑端跑一遍 1,051 个蜻蜓台的 32k 审计（复用 `validation/radio-source-audit-*` 的方法：读到连续完整帧，记录采样率/声道）。
- 若可用率高：`nrf_catalog_url()` 增加码率选择——环 ≤16 KiB 时用 32k，32 KiB 时可用 64k；32k 打开失败自动回落 64k。工牌喇叭上 32k 与 64k 听感差别预计很小，需要用户试听确认。
- 32k 流可能是 22.05/24 kHz，会走“不抽取”路径，DSP 负载也更低。
验收：审计 JSON 入 `validation/`；设备上同台 32k vs 64k 各连播 10 分钟，对比 `rebuffers`。

### P0-D 明文 HTTP 实验（可能腾出最多内存）
- `lhttp.qtfm.cn` 是否接受 `http://` **未验证**（Claude 所在环境走代理，得到的 403 来自代理，不算数）。请在用户电脑或设备上直接测。
- 若可用：对蜻蜓台优先 http，失败回落 https。电台是公开内容、不带凭证，明文风险可接受，但要写进 README 的说明里。
- 在设备上记录同一台 http vs https 打开后的 `heap/largest` 差值。如果能稳定拿到 32 KiB 环并让 AAC 分配成功，P1-F 就有解了。

### P1-E 重连更快
- 研究 `CONFIG_ESP_TLS_CLIENT_SESSION_TICKETS` / 会话复用，减少重连握手时间（如果 P0-D 成立则不重要）。
- 重连间隔 500 ms 固定 → 首次立刻重试，之后退避。

### P1-F AAC 17 台
1. 先找这 17 台的 MP3 替代源（同台不同 CDN），不能用别的台冒充。
2. 找不到时：列表里标“暂不支持”、扫台自动跳过，不要让用户落到 FAILED 画面。
3. P0-D 若腾出内存，再尝试在环之前先分配 AAC-LC 解码器；HE-AAC（SBR）仍属后续。

### P1-G 失效台与扫台体验
- 台源审计：7 个 404、8 个超时。`stations.json` 已有 `health` 字段，确认这些台在扫台/随机/“全国”里排到最后或隐藏，收藏里保留并显示状态。
- 长按下键扫台时，某台 3 s 内连不上自动跳下一台。

### P1-H 采样率切换（只观察，不先改）
- `nabo_apps.cc` 在 `nabo_radio_output_rate()` 变化时 `EnableOutput(false)` + `SetDuplexSampleRate()`；05 恰好挂在首次切 24 kHz 之后。03 未见挂起，但请用 P0-A 遥测在 50 次切台测试里确认。若复现，再考虑固定输出采样率 + 软件重采样（`nabo_radio_stream.h` 里已有未使用的 `nabo_radio_resampler`）。

### P2 仓库与发布卫生
- 公开仓库约 300 MB：14 段 mp4、美术 zip、checkpoint ELF/BIN 都在 git 里。以后的大文件放 Release 附件或 Git LFS；**不要改写历史**，除非用户另行同意。
- `design/nabo-radio/art/.library-transfer/` 是 Codex 连接器上传脚本（从环境变量读 `CODEX_CONNECTORS_TOKEN`，没有硬编码密钥），与项目无关，建议从 main 移除。
- `User-Agent: NABO-FM/0.2` 改成跟版本号一致（1.0.x）。
- README 首屏信息很密：把“已知限制”压成 3 行，加一句“哪些台最稳”（审计健康的 MP3 台）。

### 以后再说
边播边语音 + 20% 压低：两路音频 + TLS + Opus 在 C3 上大概率放不下，等 P0-C/D 的内存结论出来再评估。ICY 曲名：蜻蜓源不提供，收益很低。

## 3. 建议节奏

| 候选 | 内容 | 风险 |
|---|---|---|
| 06 | P0-A 遥测 + P0-B 断粮策略 | 低：不改内存布局 |
| 07 | P0-C 32k（先完成电脑端审计）| 中：改 URL 规则 |
| 08 | P0-D http 优先 + P1-F AAC 尝试 | 中：取决于实测 |

每个候选：ESP-IDF 6.0.2 构建 → `python tools/run_tests.py` 全过 → 用户确认后只写应用 → 3 个固定台各连播 ≥10 分钟 + 20 次切台，记录遥测 → 用户试听 → 才能说“改善”。把每轮的脱敏明细写进 `validation/`，并更新 `docs/VALIDATION.md` 和根 `HANDOFF.md`。

## 4. 关键代码位置

- 网络任务、环、预缓冲、解码循环：`xiaozhi-esp32/main/boards/folotoy/ai-passport-nabo/nabo_radio.c`（`network_worker`、`nabo_radio_begin`、`nabo_radio_read`、`nabo_radio_finish`）
- 扬声器所有者 20 ms 时钟、沙沙声、采样率切换、FM-Audio 日志：同目录 `nabo_apps.cc`（约 420–520 行）
- DSP / 抽取：`nabo_radio_dsp.c`；URL 生成：`nabo_radio_catalog.c`；台表源：`design/nabo-radio/catalog/stations.json` + `design/nabo-radio/tools/build_catalog.py`
- 宿主测试：`test_radio_engine.py`、`tests/radio_host/`
