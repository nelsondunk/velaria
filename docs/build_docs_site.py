#!/usr/bin/env python3
"""Renders the alpha docs (Markdown in docs/) into one artifact page: docs/site/index.html + ref/*.png."""
import html
import re
import shutil
from pathlib import Path

import markdown

ROOT = Path(__file__).resolve().parent
OUT = ROOT / "site"
PAGES = [
    ("setup", "Setup", "alpha-setup-guide.md"),
    ("effects", "Effects in Resolve", "howto-velaria-effects.md"),
    ("headset", "Headset reference", "headset-reference.md"),
    ("regression", "Macro regression", "macro-regression.md"),
]


def render(md_path):
    text = (ROOT / md_path).read_text(encoding="utf-8")
    body = markdown.markdown(text, extensions=["tables", "fenced_code"])
    # the first H1 is the page title; the page shows it in its own header
    m = re.match(r"\s*<h1>(.*?)</h1>", body, re.S)
    title = html.unescape(m.group(1)) if m else md_path
    body = body[m.end():] if m else body
    # make section anchors for the H2s
    def anchor(mm):
        t = re.sub(r"<[^>]+>", "", mm.group(1))
        slug = re.sub(r"[^a-z0-9]+", "-", t.lower()).strip("-")
        return f'<h2 id="{slug}">{mm.group(1)}</h2>'
    body = re.sub(r"<h2>(.*?)</h2>", anchor, body)
    return title, body


CSS = """
/* Layout: a fixed left rail of pages on wide screens, a top strip of chips on phones; one reading column of ~70ch. */
:root {
  --ground: #15181d; --step: #1d222a; --step-2: #262c35; --well: #0d0f13;
  --ink: #f2f3f5; --ink-2: #9a9ea5; --rule: #ffffff14;
  --copper: #e0a97a; --copper-ink: #15181d; --ice: #8fc1d9; --signal: #ff5a4d;
  --display: "Saira Extra Condensed", "Saira", "Arial Narrow", system-ui, sans-serif;
  --body: "Red Hat Text", system-ui, -apple-system, "Segoe UI", sans-serif;
  --mono: ui-monospace, "SF Mono", Menlo, Consolas, monospace;
  color-scheme: dark;
}
@media (prefers-color-scheme: light) { :root:not([data-theme="dark"]) {
  --ground: #f3f1ec; --step: #ffffff; --step-2: #e9e5dd; --well: #e2ded6; --ink: #1b1d22; --ink-2: #5d6168; --rule: #00000014;
  --copper: #b8703f; --copper-ink: #ffffff; --ice: #2f6f8f; --signal: #c93a2e; color-scheme: light; } }
:root[data-theme="light"] {
  --ground: #f3f1ec; --step: #ffffff; --step-2: #e9e5dd; --well: #e2ded6; --ink: #1b1d22; --ink-2: #5d6168; --rule: #00000014;
  --copper: #b8703f; --copper-ink: #ffffff; --ice: #2f6f8f; --signal: #c93a2e; color-scheme: light; }
* { box-sizing: border-box; }
body { margin: 0; background: var(--ground); color: var(--ink); font-family: var(--body); font-size: 17px; line-height: 1.6; -webkit-font-smoothing: antialiased; }
body::before { content: ""; position: fixed; inset: 0 0 auto; height: env(safe-area-inset-top, 0px); z-index: 10; background: var(--ground); }
a { color: var(--copper); }
.frame { display: grid; grid-template-columns: 260px minmax(0, 1fr); min-height: 100%; }
.rail { position: sticky; top: env(safe-area-inset-top, 0px); align-self: start; height: calc(100vh - env(safe-area-inset-top, 0px)); overflow: auto; padding-block: 28px; padding-left: max(16px, env(safe-area-inset-left, 0px)); padding-right: 16px; border-right: 1px solid var(--rule); background: var(--step); }
.brand { font-family: var(--display); font-weight: 800; text-transform: uppercase; font-size: 30px; line-height: 1; letter-spacing: 0.02em; margin: 0 0 4px; }
.brand small { display: block; font-family: var(--body); font-weight: 600; font-size: 12px; letter-spacing: 0.18em; color: var(--ink-2); margin-top: 8px; }
.pages { list-style: none; margin: 28px 0 0; padding: 0; display: flex; flex-direction: column; gap: 4px; }
.pages a { display: block; padding: 10px 12px; border-radius: 10px; color: var(--ink-2); text-decoration: none; font-weight: 600; }
.pages a:hover { color: var(--ink); background: var(--step-2); }
.pages a[aria-current="page"] { color: var(--copper-ink); background: linear-gradient(180deg, #f4cfa8 0%, #e3a676 42%, #cd8a5b 62%, #e2a878 100%); box-shadow: inset 0 1px 0 #ffffff99, inset 0 -1px 0 #7a442280; }
.toc { margin: 22px 0 0; padding: 0; list-style: none; font-size: 14px; display: flex; flex-direction: column; gap: 2px; }
.toc a { color: var(--ink-2); text-decoration: none; padding: 4px 12px; display: block; border-left: 2px solid var(--rule); border-radius: 0; }
.toc a:hover { color: var(--ink); border-left-color: var(--copper); }
.rail-foot { margin-top: 28px; font-size: 12px; color: var(--ink-2); line-height: 1.5; }
main { padding-block: 40px 96px; padding-left: max(16px, env(safe-area-inset-left, 0px)); padding-right: max(16px, env(safe-area-inset-right, 0px)); }
.page { max-width: 72ch; margin: 0 auto; min-width: 0; }
.page[hidden] { display: none; }
.eyebrow { font-size: 12px; font-weight: 700; letter-spacing: 0.2em; text-transform: uppercase; color: var(--copper); margin: 0 0 10px; }
h1 { font-family: var(--display); font-weight: 800; text-transform: uppercase; font-size: clamp(38px, 6vw, 64px); line-height: 0.95; letter-spacing: -0.005em; margin: 0 0 18px; text-wrap: balance; }
h2 { font-family: var(--display); font-weight: 800; text-transform: uppercase; font-size: 30px; line-height: 1; margin: 48px 0 14px; padding-top: 22px; border-top: 1px solid var(--rule); text-wrap: balance; }
h3 { font-size: 18px; margin: 28px 0 8px; }
p, li { max-width: 70ch; }
ul, ol { padding-left: 22px; }
li { margin: 4px 0; }
strong { font-weight: 700; }
code { font-family: var(--mono); font-size: 0.88em; background: var(--step-2); padding: 1px 6px; border-radius: 6px; }
pre { background: var(--well); border: 1px solid var(--rule); border-radius: 12px; padding: 14px 16px; overflow-x: auto; }
pre code { background: none; padding: 0; font-size: 14px; }
table { border-collapse: collapse; width: 100%; font-size: 15px; margin: 12px 0; }
.table-wrap { overflow-x: auto; }
th, td { text-align: left; vertical-align: top; padding: 9px 10px; border-bottom: 1px solid var(--rule); }
th { font-size: 12px; letter-spacing: 0.12em; text-transform: uppercase; color: var(--ink-2); font-weight: 700; }
img { max-width: 100%; height: auto; border-radius: 14px; border: 1px solid var(--rule); background: #15181d; margin: 18px 0 6px; display: block; }
img + p em, p > em:only-child { color: var(--ink-2); font-size: 14px; }
.note { border-left: 3px solid var(--ice); padding: 10px 14px; background: var(--step); border-radius: 0 10px 10px 0; margin: 18px 0; }
.chips { display: none; }
:focus-visible { outline: 2px solid var(--copper); outline-offset: 3px; }
@media (max-width: 860px) {
  .frame { grid-template-columns: 1fr; }
  .rail { position: static; height: auto; border-right: 0; border-bottom: 1px solid var(--rule); padding-block: 18px; padding-right: max(16px, env(safe-area-inset-right, 0px)); }
  .pages { flex-direction: row; flex-wrap: wrap; margin-top: 14px; }
  .toc, .rail-foot { display: none; }
  main { padding-block: 24px 64px; }
}
@media (prefers-reduced-motion: no-preference) { html { scroll-behavior: smooth; } }
"""

JS = """
(function () {
  var pages = Array.prototype.slice.call(document.querySelectorAll('.page'));
  var links = Array.prototype.slice.call(document.querySelectorAll('.pages a'));
  var tocs = Array.prototype.slice.call(document.querySelectorAll('.toc'));
  function show(id) {
    var found = pages.some(function (p) { return p.dataset.page === id; });
    if (!found) id = pages[0].dataset.page;
    pages.forEach(function (p) { p.hidden = p.dataset.page !== id; });
    links.forEach(function (a) { if (a.dataset.page === id) a.setAttribute('aria-current', 'page'); else a.removeAttribute('aria-current'); });
    tocs.forEach(function (t) { t.hidden = t.dataset.page !== id; });
    try { localStorage.setItem('kalyx-docs-page', id); } catch (e) {}
  }
  function fromHash() {
    var h = (location.hash || '').replace('#', '');
    if (h) { var parts = h.split('--'); show(parts[0]); if (parts[1]) { var el = document.getElementById(parts[1]); if (el) el.scrollIntoView(); } return; }
    var saved = null; try { saved = localStorage.getItem('kalyx-docs-page'); } catch (e) {}
    show(saved || pages[0].dataset.page);
  }
  window.addEventListener('hashchange', fromHash);
  fromHash();
})();
"""


def main():
    OUT.mkdir(exist_ok=True)
    ref_src = ROOT.parent / "quest" / "host" / "ref"
    ref_dst = OUT / "ref"
    ref_dst.mkdir(exist_ok=True)
    for p in ref_src.glob("*.png"):
        shutil.copy(p, ref_dst / p.name)
    nav, tocs, sections = [], [], []
    for pid, label, md in PAGES:
        title, body = render(md)
        heads = re.findall(r'<h2 id="([^"]+)">(.*?)</h2>', body)
        nav.append(f'<li><a href="#{pid}" data-page="{pid}">{html.escape(label)}</a></li>')
        toc_items = "".join(f'<li><a href="#{pid}--{hid}">{re.sub(r"<[^>]+>", "", ht)}</a></li>' for hid, ht in heads)
        tocs.append(f'<ul class="toc" data-page="{pid}" hidden>{toc_items}</ul>')
        body = body.replace("<table>", '<div class="table-wrap"><table>').replace("</table>", "</table></div>")
        sections.append(f'<article class="page" data-page="{pid}" hidden><p class="eyebrow">Velaria · {html.escape(label)}</p><h1>{html.escape(title)}</h1>{body}</article>')
    page = f"""<title>Velaria Docs</title>
<meta name="claude:viewport-fit-sides" content="cover">
<link rel="preconnect" href="https://fonts.googleapis.com">
<link rel="preconnect" href="https://fonts.gstatic.com" crossorigin>
<link rel="stylesheet" href="https://fonts.googleapis.com/css2?family=Saira+Extra+Condensed:wght@800&family=Red+Hat+Text:wght@500;600;700&display=swap">
<style>{CSS}</style>
<div class="frame">
  <nav class="rail" aria-label="Pages">
    <p class="brand">Velaria<small>Docs · 1.0.9</small></p>
    <ul class="pages">{"".join(nav)}</ul>
    {"".join(tocs)}
    <p class="rail-foot">Mac + DaVinci Resolve Studio + Meta Quest. Pictures of the headset UI are rendered by the app itself, so they match the build they ship with.</p>
  </nav>
  <main>{"".join(sections)}</main>
</div>
<script>{JS}</script>
"""
    (OUT / "index.html").write_text(page, encoding="utf-8")
    print("wrote", OUT / "index.html", len(page), "bytes;", len(list(ref_dst.glob("*.png"))), "images")


if __name__ == "__main__":
    main()
