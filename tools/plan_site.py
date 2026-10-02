"""Render PLAN.md into site/index.html (git-ignored). Re-run after editing PLAN.md.

The Markdown is embedded in the page and rendered in the browser with marked (cdnjs), so the page
opens straight from disk (file://) with no server.
"""
import pathlib

ROOT = pathlib.Path(__file__).resolve().parent.parent
md = (ROOT / "PLAN.md").read_text(encoding="utf-8")
out = ROOT / "site" / "index.html"
out.parent.mkdir(exist_ok=True)

PAGE = """<!doctype html>
<html lang="en">
<head>
<meta charset="utf-8">
<meta name="viewport" content="width=device-width, initial-scale=1">
<title>F-117A Port Plan</title>
<style>
:root { --bg:#f7f7f5; --fg:#1d1d1b; --muted:#6b6b66; --card:#fff; --line:#deded8; --accent:#2b5fab;
        --code:#efefea; --h:#3d7a3d; --s:#2b5fab; --o:#8a4bb0; --done:#3d7a3d; }
@media (prefers-color-scheme: dark) { :root:not([data-theme="light"]) {
  --bg:#141413; --fg:#e8e8e3; --muted:#9a9a93; --card:#1d1d1b; --line:#33332f; --accent:#7aa7e8;
  --code:#262624; --h:#7cc07c; --s:#7aa7e8; --o:#c49ae0; --done:#7cc07c; } }
* { box-sizing:border-box; }
body { margin:0; background:var(--bg); color:var(--fg);
       font:15px/1.55 system-ui, -apple-system, "Segoe UI", sans-serif; }
.wrap { display:grid; grid-template-columns:240px minmax(0,1fr); gap:32px; max-width:1240px;
        margin:0 auto; padding:24px 16px 64px; }
nav { position:sticky; top:16px; align-self:start; max-height:calc(100vh - 32px); overflow:auto;
      font-size:13px; }
nav a { display:block; color:var(--muted); text-decoration:none; padding:3px 0; }
nav a.h3 { padding-left:12px; }
nav a:hover { color:var(--accent); }
main { min-width:0; }
h1 { font-size:26px; margin:0 0 12px; }
h2 { font-size:20px; margin:36px 0 10px; padding-bottom:6px; border-bottom:1px solid var(--line); }
h3 { font-size:16px; margin:24px 0 8px; }
a { color:var(--accent); }
code { background:var(--code); padding:1px 5px; border-radius:4px; font-size:13px; }
pre { background:var(--code); padding:12px 14px; border-radius:8px; overflow:auto; }
pre code { padding:0; background:none; }
.tbl { overflow-x:auto; margin:10px 0 16px; }
table { border-collapse:collapse; width:100%; background:var(--card); font-size:14px; }
th, td { border:1px solid var(--line); padding:6px 9px; text-align:left; vertical-align:top; }
th { background:var(--code); font-weight:600; }
.b { display:inline-block; min-width:22px; text-align:center; padding:0 6px; border-radius:10px;
     font-weight:700; font-size:12px; color:#fff; }
.b.H { background:var(--h); } .b.S { background:var(--s); } .b.O { background:var(--o); }
td.done { color:var(--done); font-weight:600; }
.meta { color:var(--muted); font-size:12px; margin-bottom:20px; }
@media (max-width: 800px) { .wrap { grid-template-columns:1fr; } nav { position:static; max-height:none; } }
</style>
</head>
<body>
<div class="wrap">
<nav id="toc"></nav>
<main>
<div class="meta">Generated from PLAN.md by tools/plan_site.py</div>
<div id="doc"></div>
</main>
</div>
<script type="text/markdown" id="src">__MD__</script>
<script src="https://cdnjs.cloudflare.com/ajax/libs/marked/12.0.2/marked.min.js"></script>
<script>
const src = document.getElementById('src').textContent;
const doc = document.getElementById('doc');
doc.innerHTML = window.marked ? marked.parse(src) : '<pre>' + src.replace(/[<&]/g, c => c === '<' ? '&lt;' : '&amp;') + '</pre>';
doc.querySelectorAll('table').forEach(t => {
  const w = document.createElement('div'); w.className = 'tbl'; t.replaceWith(w); w.appendChild(t);
});
doc.querySelectorAll('td').forEach(td => {
  const t = td.textContent.trim();
  if (/^[HSO]$/.test(t)) td.innerHTML = '<span class="b ' + t + '">' + t + '</span>';
  else if (/^done/.test(t)) td.classList.add('done');
});
const toc = document.getElementById('toc');
doc.querySelectorAll('h2, h3').forEach((h, i) => {
  h.id = 'h' + i;
  const a = document.createElement('a');
  a.href = '#' + h.id; a.textContent = h.textContent; a.className = h.tagName.toLowerCase();
  toc.appendChild(a);
});
</script>
</body>
</html>
"""

# <script> content is raw text: only a closing tag inside it needs breaking up.
out.write_text(PAGE.replace("__MD__", md.replace("</script", "<\\/script")), encoding="utf-8")
print(f"wrote {out.relative_to(ROOT)}")
