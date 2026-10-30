# 刷写与恢复 · NABO FM v1.0.1 体验版

本版使用封存 06r5 精确镜像，已在已配置设备上完成应用区升级与保护区核对；台 26 短测及 2 轮主页往返通过，剩余完整验收由用户主动提前结束。**完整组合未在空白设备重新执行全套首装验收。** 具体范围见 [发行说明](RELEASE_FM_v1.0.1.md)。

社区 / 网页使用的 `ai-passport-nabo-xiaozhi-radio-v1.0.1-full.bin` 从 `0x0` 写入，**会清空 Wi-Fi 配网、激活和 NVS 设置**，需重新配网 / 激活。它只由构建输出合并，间隙为 `0xFF`，不含设备备份或个人凭据。

希望保留数据时，先核对本机型号、旧布局与目标镜像，再使用 ZIP 中的受保护分区工具。版本名称不能证明镜像兼容。

## 镜像与保护区

仅支持 ESP32-C3 / 8 MB 的 FoloToy AI Passport。使用自己的端口和设备 MAC；下方 `YOUR_PORT` / `YOUR_DEVICE_MAC` 都是待替换占位符。

| 内容 | 偏移 | 分区上限 |
|---|---|---|
| bootloader.bin | `0x0` | `0x8000` |
| partition-table.bin | `0x8000` | `0x1000` |
| NVS（分区工具保留） | `0x9000` | `0x6000` |
| PHY（分区工具保留） | `0xf000` | `0x1000` |
| xiaozhi.bin | `0x10000` | `0x3f0000` |
| assets.bin | `0x400000` | `0x300000` |
| files（分区工具保留） | `0x700000` | `0x100000` |

ZIP 是独立镜像集合，不能直接写到 `0x0`。另附 full BIN 与下述四镜像 install 模式的数据保留行为不同：full BIN 会覆盖空白 NVS；分区 install 写四个构建镜像，并保留上述 NVS、PHY、files 范围。不要使用 `erase-flash`，也不要传播已配置设备的整片备份。

## 检查包与设备

下载并解压 v1.0.1 ZIP，在解压目录执行：

```powershell
python -m pip install -r requirements-flash.txt
python tools/flash_release.py --package . --mode inspect
python -m esptool --chip esp32c3 --port YOUR_PORT chip-id
```

`inspect` 默认只检查本地包；`chip-id` 用于读取自己的设备身份。写入前关闭串口监视器。工具只有明确指定 upgrade / install、端口和 MAC 才会写入；它核对 ESP32-C3、8 MB、未启用安全启动 / Flash 加密，每条设备命令核对 MAC，并先备份、校验完整 8 MB。

## 升级：应用和资源

```powershell
python tools/flash_release.py --package . --mode upgrade --port YOUR_PORT --expected-mac YOUR_DEVICE_MAC
```

包内 `upgrade` 要求当前 bootloader 和分区表与包内镜像精确匹配，随后写 `xiaozhi.bin` 与 `assets.bin`，校验已写镜像及列明的保护区，保留 NVS、PHY 和 files。NABO 动画、字体和台表使用资源分区。工具完成后重启，再核对画面、按键、联网和语音。

若 bootloader / 分区不同，upgrade 在写入前停止。历史 v1.0.0 设备曾保留兼容但字节不同的 bootloader，不能因同为 NABO FM 就推定适合本次 upgrade 或仅应用写入。先核对旧布局和兼容性，再决定受保护的安装方式；不绕过检查直接裸写。

### 仅应用更新的前提

只有当前 bootloader、分区表和资源均已确认与目标匹配，且已有完整新备份和写后保护区校验方案时，才具备仅写 `0x10000` 应用的前提。此次 06r3 → 06r4 → 06r5 的专门校验流程符合该条件，并核对了应用尾部及全部未写字节；这个设备上的结果不泛化为所有 v1.0.0 或其他固件设备。

公开包内工具提供 `inspect / upgrade / install` 三种模式，**没有 app-only 模式**；不要自行删减命令参数来模拟本次私有设备流程。

## 受保护的四镜像安装

```powershell
python tools/flash_release.py --package . --mode install --port YOUR_PORT --expected-mac YOUR_DEVICE_MAC
```

分区 install 写 bootloader、分区表、应用和资源，保留固定地址的 NVS、PHY、files，并产生本机私有整片备份。它会覆盖这些目标镜像范围内原固件的内容；旧布局中的其他资源或记录可能位于被覆盖范围，必须先核对布局。

NVS 字节保留不代表其他固件的账户或设置与本版兼容。首次使用小智仍按屏幕完成配网 / 激活；完整首装尚未实机验收，不能把本轮应用区升级当作首装证据。

## 备份与恢复

工具将备份、过程日志和校验记录放在本机 `private/`，其中可能含配网和账户信息，不随 Release 发布。恢复使用**同一块设备自己的备份**，重新核对设备 MAC、备份 SHA256、容量与备份时布局。

工具只处理未加密的普通设备，不修改 eFuse。自定义分区没有 OTA 槽，后续仍使用 USB 分区刷写包。失败时先保留记录并核对原因；自动复位成功不代表功能验收完成。
