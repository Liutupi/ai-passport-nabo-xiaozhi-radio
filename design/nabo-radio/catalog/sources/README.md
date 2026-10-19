# 电台来源

| 文件 | 来源 | 取法 |
|---|---|---|
| `qingting_web.tsv` | 蜻蜓FM网页版公开电台页 `qtfm.cn/radiopage/<地区或类型>/<页>` | 在国内网络的浏览器里运行 `tools/crawl_qingting.js`（同源请求，每页间隔 120 ms），字段：蜻蜓编号、台名、热度、地区、类型 |
| `nabo.json` | 早期 NABO 电台版 47 台（用户 Tab5 SD 卡） | `tools/import_sources.py` |
| `weibaohui.json` | 魏宝辉《网络收音机》内置台目（仓库无许可证，只取台名和地址这类公开信息） | 同上 |
| `shulinbao.json` | 小海马《港·新·马 网络电台》（MIT） | 同上 |

蜻蜓直播流地址由编号确定：`https://lhttp.qtfm.cn/live/<编号>/64k.mp3`。
