# 来源与许可

## XiaoZhi

基于 <https://github.com/78/xiaozhi-esp32> 提交 `4632dc51f0a5ad26e08542e131e6e48da41e4ff3`，上游 MIT 许可证完整保留在根目录及 `xiaozhi-esp32/LICENSE`。新增/修改的项目代码沿用 MIT；下列第三方内容按其各自条款。

## NABO FM 电台版新增资源

DJ 动画和道具来自拥有者已验收的 NABO 电台素材，保持其造型；`radio_sprites.bin` 为 13 动作 247 帧。NABO 形象和美术不因源码 MIT 许可成为 MIT 素材。

台名、地区、类型及公开流地址合并自蜻蜓公开页面、既有 NABO 台表、魏宝辉台表和小海马港新马台表，仅使用公开台目事实，未使用无许可来源的播放器代码。来源：<https://www.qtfm.cn/>、<https://github.com/weibaohui/aipassport-radio>、<https://github.com/shulinbao/ai-passport-radio>。快照日期为 2026-10-19。

电台 name/logo/clock 字体子集来自 LXGW WenKai Regular，ui/led 子集来自 Source Han Sans SC Normal，均保留 OFL，原文在板型的 `OFL-LXGW.txt`、`OFL-SourceHanSans.txt`。源字体及输出 SHA256 见 `docs/radio-assets.json`。Espressif AAC 按 esp_audio_codec 自身许可使用。

## Helix MP3（本电台版与 v1.2.0 及更早版本）

本电台版重新引入 Helix MP3（RealNetworks，RPSL/RCSL），不改授 MIT。覆盖源码和原始许可在板型 `helix/`，定制分配器在 `nabo_helix_buffers.c`。开发包同时携带到 `third_party/helix-mp3/`。中间的深潜/牌桌版未使用 Helix，历史标签分别记录。

## 字体与组件

界面中文字体 `nabo_menu_font_16` / `nabo_ui_font_18` / `nabo_table_font_12` / `nabo_table_font_24` 子集自思源黑体 Source Han Sans CN Medium（Copyright 2014-2025 Adobe，保留字体名 “Source”，OFL 1.1，全文 `OFL-SourceHanSans.txt`）；子集以 nabo_* 命名，不使用保留字体名。v1.1.0 及更早版本使用 LXGW WenKai（OFL 1.1，`OFL-LXGW.txt` 保留以对应旧版源码）。节奏回声的海报标题字体 `nabo_poster_font_28/48` 子集自 Archivo Black（Copyright 2017 The Archivo Black Project Authors，OFL 1.1，全文 `OFL-ArchivoBlack.txt`），小号标签字体 `nabo_mono_font_12` 子集自 Space Mono Bold（Copyright 2016 The Space Mono Project Authors，OFL 1.1，全文 `OFL-SpaceMono.txt`）。字体由 `tools/generate_fonts.py` 用 lv_font_conv 1.5.3 生成，来源校验和记录在 `game-art/fonts.json`；源 TTF 不入库。ESP-IDF、LVGL、Espressif/其他 IDF 组件分别保留其上游许可，通过 `main/idf_component.yml` 与 `dependencies.lock` 解析；MIT 不覆盖这些依赖的独立条款。

## NABO 和游戏资源

NABO 美术来自项目拥有者提供的原图与此前 NABO 工牌资源。医院、书房、播客、木鱼、咖啡、靶心平台为本定制项目制作；游戏机制和等距画面参考微信《跳一跳》。没有取得或使用微信原版源码。

蓄力、起跳、正中 PCM 参考项目拥有者指定的“新空气”AI Passport 社区版本，来源 <https://ai-passport.folotoy.cn/plays/778/> 的公开固件；来源/样本校验和见 `game-art/original-sound-reference.json`。落地/跌落及淡化蓄力变体在本地制作。社区页面没有为这些声音提供已确认的独立开源许可，因此**不将它们声明为 MIT 或公有领域**。素材权利与源码许可分开；再分发/替换素材时须保留来源并核实适用条款。

NABO 牌桌的 NABO 荷官造型、起始图和动作视频由项目拥有者使用 Codex（出图）和 Grok Imagine（图生视频）为本项目制作，NABO 形象权利同上文 NABO 美术，**不因源码的 MIT 许可证而成为 MIT 素材**；`table_assets/table_sprites.bin` 由这些视频抽帧生成。21 点和大话骰为公共规则的传统游戏；界面、扑克牌与骰盅绘制、交接流程和合成音效为本项目原创实现，立体骰子沿用本项目此前摇骰子程序的渲染器。

v1.3.x 深潜版的 NABO 潜水帧见 <https://github.com/Liutupi/nabo-dive>；深潜已在本版移除。

节奏回声（ECHO CLUB）的“先听示范、再拍回去”玩法受 AI Passport 社区《节奏闯关 · COPYCAT CLUB》启发；界面、关卡节奏型、合成音色和全部代码均为本项目原创实现，未使用该作品的源码、图像或声音。

本项目不包含微信客户端、社区整片固件、私人账号数据或他人的设备备份。
