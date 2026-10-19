#!/usr/bin/env python3
"""把各来源的电台清单解析成统一的 JSON（catalog/sources/<来源>.json）。

来源只取电台名称和流地址这类公开信息，不复制任何源码。
用法：python tools/import_sources.py --nabo <nabo_radio_stations.h>
        --weibaohui <radio_catalog.h> --shulinbao <radio_catalog.c>
"""
import argparse, json, re
from pathlib import Path

OUT = Path(__file__).resolve().parents[1] / "catalog" / "sources"

def qt_id(url):
    m = re.search(r"/live/(\d+)/", url)
    return m.group(1) if m and ("qtfm" in url or "qingting" in url) else None

def nabo(path):
    text = Path(path).read_text(encoding="utf-8")
    cats = re.findall(r'^\s+"([^"]+)",$', text.split("nabo_radio_categories")[1].split("};")[0], re.M)
    rows = []
    for name, url, cat in re.findall(r'\{"([^"]+)", "([^"]+)", (\d+)\}', text):
        rows.append({"name": name, "url": url, "group": cats[int(cat)], "qt": qt_id(url)})
    return rows

def weibaohui(path):
    rows = []
    for name, url in re.findall(r'\{ "([^"]+)", "([^"]+)" \}', Path(path).read_text(encoding="utf-8")):
        rows.append({"name": name, "url": url.rstrip("?"), "name_truncated": True, "qt": qt_id(url)})
    return rows

def shulinbao(path):
    text = Path(path).read_text(encoding="utf-8")
    region = {"HK": "香港", "SG": "新加坡", "MY": "马来西亚"}
    rows = []
    pat = r'\{\s*\d+, RADIO_REGION_(\w+), RADIO_CODEC_(\w+), (\d+),\s*"([^"]+)", "([^"]+)",\s*"([^"]+)" \}'
    for reg, codec, freq, name, tag, url in re.findall(pat, text):
        rows.append({"name": name, "url": url, "region": region[reg], "codec": codec.lower(),
                     "freq": int(freq) / 10 if int(freq) else None, "tagline": tag})
    return rows

if __name__ == "__main__":
    ap = argparse.ArgumentParser()
    ap.add_argument("--nabo"); ap.add_argument("--weibaohui"); ap.add_argument("--shulinbao")
    a = ap.parse_args()
    OUT.mkdir(parents=True, exist_ok=True)
    meta = {
        "nabo": ("NABO 早期电台版（用户 Tab5 SD 卡）", None, nabo),
        "weibaohui": ("魏宝辉 · 网络收音机", "https://github.com/weibaohui/aipassport-radio", weibaohui),
        "shulinbao": ("小海马 · 港·新·马 网络电台（MIT）", "https://github.com/shulinbao/ai-passport-radio", shulinbao),
    }
    for key, (title, repo, fn) in meta.items():
        src = getattr(a, key)
        if not src: continue
        rows = fn(src)
        (OUT / f"{key}.json").write_text(json.dumps({"source": title, "repo": repo, "stations": rows},
                                                     ensure_ascii=False, indent=1), encoding="utf-8")
        print(f"{key}: {len(rows)}")
