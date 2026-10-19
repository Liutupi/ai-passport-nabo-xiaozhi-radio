#!/usr/bin/env python3
"""把模板、电台数据和 NABO 占位图拼成单文件原型 index.html。"""
from pathlib import Path
here = Path(__file__).parent
html = (here / "template.html").read_text(encoding="utf-8")
html = html.replace("__DATA__", (here / "data.json").read_text(encoding="utf-8"))
for k in ("off", "on", "mic", "mug", "head"):
    html = html.replace(f"__SPR_{k.upper()}__", (here / f"sprite_{k}.b64").read_text().strip())
import base64
for key, name in (("SMILEY", "smiley"), ("WENKAI", "wenkai"), ("PIXEL", "pixel"), ("SANSB", "sans_bold"), ("SANSM", "sans_med")):
    html = html.replace(f"__F_{key}__", base64.b64encode((here / "fonts" / f"{name}.woff2").read_bytes()).decode())
(here / "index.html").write_text(html, encoding="utf-8")
print("index.html", len(html.encode()) // 1024, "KB")
