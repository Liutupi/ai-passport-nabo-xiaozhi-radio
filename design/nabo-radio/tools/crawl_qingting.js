// 蜻蜓FM网页版电台目录抓取。
// 用法：在国内网络的浏览器里打开 https://www.qtfm.cn/radiopage ，把本文件粘进开发者工具控制台运行，
// 完成后执行 copy(window.__qtTSV) 把结果复制出来，保存为 catalog/sources/qingting_web.tsv。
// 只读公开的电台列表页；同源请求，每页间隔 120 ms。
(async () => {
  const sleep = ms => new Promise(r => setTimeout(r, ms));
  const regions = {"北京":3,"天津":5,"河北":7,"上海":83,"山西":19,"内蒙古":31,"辽宁":44,"吉林":59,"黑龙江":69,"江苏":85,"浙江":99,"安徽":111,"福建":129,"江西":139,"山东":151,"河南":169,"湖北":187,"湖南":202,"广东":217,"广西":239,"海南":254,"重庆":257,"四川":259,"贵州":281,"云南":291,"陕西":316,"甘肃":327,"宁夏":351,"新疆":357,"西藏":308,"青海":342,"网络台":407};
  const types = {"资讯":433,"音乐":442,"交通":429,"经济":439,"文艺":432,"都市":441,"体育":430,"双语":431,"综合":440,"生活":438,"旅游":435,"曲艺":436,"方言":434};
  const st = {};
  async function crawl(rid, field, label) {
    for (let p = 1; p <= 60; p++) {
      const html = await fetch(`/radiopage/${rid}/${p}`).then(r => r.text());
      const doc = new DOMParser().parseFromString(html, "text/html");
      const items = [...doc.querySelectorAll(".contentSec .content-item-root")];
      let fresh = 0;
      for (const it of items) {
        const a = it.querySelector("a.link"); if (!a) continue;
        const id = a.getAttribute("href").match(/\d+/)[0];
        const name = (it.querySelector(".itemTitleRadio")?.getAttribute("title") || a.innerText).trim();
        const heat = +(it.querySelector(".span-clout")?.innerText || 0);
        const s = st[id] || (st[id] = { id, name, heat, regions: [], types: [] });
        if (!s[field].includes(label)) { s[field].push(label); fresh++; }
      }
      if (!items.length || !fresh) break;
      await sleep(120);
    }
  }
  for (const [k, v] of Object.entries(regions)) await crawl(v, "regions", k);
  for (const [k, v] of Object.entries(types)) await crawl(v, "types", k);
  const day = new Date().toISOString().slice(0, 10);
  window.__qtTSV = `# 蜻蜓FM网页版电台页抓取 ${day}；字段：编号\t台名\t热度\t地区\t类型\n` +
    Object.values(st).map(s => [s.id, s.name.replace(/[\t\n]/g, " "), s.heat, s.regions.join(","), s.types.join(",")].join("\t")).join("\n") + "\n";
  console.log(`完成：${Object.keys(st).length} 台。执行 copy(window.__qtTSV) 复制结果。`);
})();
