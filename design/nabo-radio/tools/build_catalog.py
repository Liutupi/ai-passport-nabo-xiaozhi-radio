#!/usr/bin/env python3
"""合并各来源，生成统一电台表 catalog/stations.json 和原型用的精简数据。

去重规则：蜻蜓的台按编号去重，台名以蜻蜓网页为准；其他台按地址去重。
用法：python tools/build_catalog.py
"""
import json, re
from pathlib import Path
from pypinyin import lazy_pinyin, Style

ROOT = Path(__file__).resolve().parents[1]
SRC = ROOT / "catalog" / "sources"

# 刻度上的类型波段：蜻蜓的 13 个类型归并成 9 个
TYPE_BAND = {"资讯": "新闻", "综合": "综合", "音乐": "音乐", "交通": "交通", "经济": "财经",
             "文艺": "文艺", "曲艺": "文艺", "双语": "文艺", "都市": "生活", "生活": "生活",
             "旅游": "生活", "体育": "体育", "方言": "方言"}
TYPE_ORDER = ["新闻", "音乐", "交通", "财经", "文艺", "生活", "体育", "方言", "综合"]
QT_URL = "https://lhttp.qtfm.cn/live/{}/64k.mp3"


def initials(name):
    out = []
    for part in lazy_pinyin(name, style=Style.FIRST_LETTER, errors=lambda s: list(s)):
        for ch in part:
            if ch.isascii() and ch.isalpha():
                out.append(ch.lower())
    return "".join(out)


def guess_type(name):
    for kw, t in (("音乐", "音乐"), ("交通", "交通"), ("新闻", "新闻"), ("经济", "财经"), ("财经", "财经"),
                  ("文艺", "文艺"), ("戏曲", "文艺"), ("故事", "文艺"), ("生活", "生活"), ("旅游", "生活"),
                  ("体育", "体育"), ("Music", "音乐"), ("FM", "音乐")):
        if kw in name:
            return t
    return "综合"


def main():
    stations, by_qt, by_url = [], {}, {}

    def add(s):
        if s.get("qt"):
            if s["qt"] in by_qt:
                old = by_qt[s["qt"]]
                old["sources"] = sorted(set(old["sources"]) | set(s["sources"]))
                return
            by_qt[s["qt"]] = s
        else:
            key = s["url"].split("://", 1)[1].rstrip("/?")
            if key in by_url:
                by_url[key]["sources"] = sorted(set(by_url[key]["sources"]) | set(s["sources"]))
                return
            by_url[key] = s
        stations.append(s)

    # 1. 蜻蜓网页目录（台名、热度、地区、类型最全）
    for line in (SRC / "qingting_web.tsv").read_text(encoding="utf-8").splitlines():
        if not line or line.startswith("#"):
            continue
        qid, name, heat, regions, types = (line.split("\t") + ["", ""])[:5]
        regs = [r for r in regions.split(",") if r]
        typs = [t for t in types.split(",") if t]
        add({"qt": qid, "name": name.strip(), "url": QT_URL.format(qid), "codec": "mp3",
             "region": "网络台" if (not regs or regs == ["网络台"]) else regs[0],
             "types": sorted({TYPE_BAND[t] for t in typs}, key=TYPE_ORDER.index) or [guess_type(name)],
             "heat": int(heat or 0), "sources": ["qingting"]})

    # 2. 其他来源
    for key in ("nabo", "weibaohui", "shulinbao"):
        data = json.loads((SRC / f"{key}.json").read_text(encoding="utf-8"))
        for r in data["stations"]:
            qt = r.get("qt")
            region = r.get("region") or ("网络台" if not qt else "未知")
            add({"qt": qt, "name": r["name"], "url": QT_URL.format(qt) if qt else r["url"],
                 "codec": r.get("codec", "mp3"), "region": region,
                 "types": [guess_type(r["name"] + " " + r.get("tagline", ""))], "heat": 0,
                 "freq": r.get("freq"), "name_truncated": r.get("name_truncated", False),
                 "sources": [key]})

    # 体检结果：404 的台下架，修正编码标错的台
    health = json.loads((ROOT / "catalog" / "health_2026-10-19.json").read_text(encoding="utf-8"))
    dead_qt = {str(q) for q in health["dead_http404"]["qingting"]}
    dead_url = set(health["dead_http404"]["url"])
    removed = [s for s in stations if (s.get("qt") in dead_qt) or (s["url"] in dead_url)]
    stations[:] = [s for s in stations if s not in removed]
    for s in stations:
        if s["url"] in health["codec_corrections"]:
            s["codec"] = "mp3"
        s["health"] = ("untested" if s["url"].startswith("http://") else
                       "cors" if s["url"] in health["browser_cors_blocked_not_dead"] else "ok")
    print("下架（404）", len(removed))

    # 只在其他来源出现、蜻蜓目录里没有的蜻蜓编号：保留，但标记“待核实”
    for s in stations:
        s["verify_name"] = bool(s.get("qt")) and "qingting" not in s["sources"]
        s["py"] = initials(s["name"])
    stations.sort(key=lambda s: (-s["heat"], s["name"]))
    for i, s in enumerate(stations):
        s["id"] = i

    out = {"generated": "2026-10-19", "count": len(stations), "stations": stations}
    (ROOT / "catalog" / "stations.json").write_text(json.dumps(out, ensure_ascii=False, indent=1), encoding="utf-8")

    # 原型数据：[台名, 地区, 类型, 热度, 编码, 拼音首字母]
    proto = {"local": "广东", "st": [], "bands": []}
    for s in stations:
        proto["st"].append([s["name"], s["region"] if s["region"] not in ("未知",) else "", "/".join(s["types"]),
                            s["heat"], s["codec"], s["py"]])
    for t in TYPE_ORDER:
        proto["bands"].append({"name": t, "kind": "type", "ids": [s["id"] for s in stations if t in s["types"]]})
    order = ["香港", "新加坡", "马来西亚", "网络台"]
    regions = sorted({s["region"] for s in stations if s["region"] not in order + ["未知"]},
                     key=lambda r: -sum(x["heat"] for x in stations if x["region"] == r))
    for r in regions + order:
        ids = [s["id"] for s in stations if s["region"] == r]
        if ids:
            proto["bands"].append({"name": r, "kind": "region", "ids": ids})
    (ROOT / "prototype" / "data.json").write_text(json.dumps(proto, ensure_ascii=False, separators=(",", ":")), encoding="utf-8")

    from collections import Counter
    print("总数", len(stations))
    print("来源", Counter(src for s in stations for src in s["sources"]))
    print("只在一个来源", Counter(s["sources"][0] for s in stations if len(s["sources"]) == 1))
    print("编码", Counter(s["codec"] for s in stations))
    print("台名待核实（蜻蜓目录里没有的蜻蜓编号）", sum(s["verify_name"] for s in stations))
    print("波段", [(b["name"], len(b["ids"])) for b in proto["bands"]][:12], "…")


if __name__ == "__main__":
    main()
