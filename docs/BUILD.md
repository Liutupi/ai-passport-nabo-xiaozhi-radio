# 构建与测试

使用 ESP-IDF 6.0.2、Python 3.11+、目标 ESP32-C3。初始化 IDF 后在根目录执行 `python tools/build.py`。输出在 `xiaozhi-esp32/build-release/`，包含固定配置、应用、bootloader、分区表和资源镜像。正常构建使用仓库内 `radio_assets/*.bin`，不需要外部美术输入。组件由 `dependencies.lock` 解析，不提交 managed_components。

上游板型入口仍为 `python scripts/build.py folotoy/ai-passport-nabo --name ai-passport-nabo --language zh-CN`。它会更新本地构建状态；发行候选使用根目录固定配置工具，并检查芯片、单应用分区和 NVS 兼容边界。

```powershell
python -m pip install -r requirements-dev.txt
# GCC/G++ 在 PATH；Windows 推荐 w64devkit，CXX 用可执行名或正斜杠路径
$env:CXX='g++'
python tools/run_tests.py
python tools/render_radio_preview.py
```

回归直接编译生产代码，含上游 82 项、刷写包 7 项、语音 55 例、PCM 分块、DSP 14 项、Helix 分配器和旧电台 48 项。新增全部 247 帧精灵逐像素对拍、1,105 台二进制目录、按键时序、生产场景渲染，以及离线 MP3 经压缩缓冲到 DSP 的逐采样一致性和生命周期测试。网络、NVS、I2S、背光的宿主替身不构成真实设备测试。

## 重新制作资源

`tools/pack_fm_assets.py` 需要设计目录 `nabo-radio`（台表、已打包动画和道具）及外部字体：

```powershell
python tools/pack_fm_assets.py --design ../nabo-radio --wenkai /path/LXGWWenKai-Regular.ttf --sans /path/SourceHanSansSC-Normal.otf
```

输出 NFC1 台表、NFM1 精灵、NFF1 4-bit 字体和 NFP1 道具，均直接从资源分区读取。字体名、SHA256、字形数和输出大小记录在 `radio-assets.json`。二进制变化会触发资源镜像重建；升级必须同时写 app + assets。

动画打包器和原视频暂留在独立设计目录。字体覆盖现有台名、地区和界面；新增缺字当前显示问号，回退到通用字库尚未接入。小智 common 20 px 通用字库保留，不能为腾空间删掉。

## 本地开发包

```powershell
python tools/package_release.py --version 0.2.0 --build xiaozhi-esp32/build-release
python tools/flash_release.py --package dist/ai-passport-nabo-xiaozhi-radio-v0.2.0 --mode inspect
```

开发包含四个构建输出、文档、校验和、OFL 字体许可及 Helix 对应源码/原始许可。它不是经过设备验收的正式 Release，不含对话、配网、NVS 或设备备份。刷写前需核对连接设备、MAC 和当前 bootloader/分区；候选构建不自动写入工牌。
