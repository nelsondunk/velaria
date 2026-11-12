#!/usr/bin/env python3
"""Builds the public Velaria docs site from the Markdown in docs/public/.

    python3 docs/build_docs_site.py            writes docs/site/*.html + docs/site/ref/*.png
    SUPPORT_EMAIL=help@example.com SITE_URL=https://example.com/velaria DOCS_URL=https://example.com/velaria/docs \\
        python3 docs/build_docs_site.py        fills the placeholders for a real publish

One static HTML file per page, no JavaScript, styles inline. The colours and type are the sales page's
(site/index.html :root). The build refuses to write a page that contains an internal name or a personal path.
Needs the `markdown` package (pip install markdown) and, for image sizes, Pillow (optional).
"""
import html
import json
import os
import re
import shutil
import sys
from pathlib import Path

import markdown

ROOT = Path(__file__).resolve().parent
SRC = ROOT / "public"
OUT = ROOT / "site"
REF_SRC = ROOT.parent / "quest" / "host" / "ref"

SUPPORT_EMAIL = os.environ.get("SUPPORT_EMAIL", "")          # empty = leave the [SUPPORT EMAIL] placeholder and warn
SITE_URL = os.environ.get("SITE_URL", "#SITE-URL")           # the Velaria sales page
DOCS_URL = os.environ.get("DOCS_URL", "").rstrip("/")        # where this folder will live; empty = no canonical tags
VERSION = os.environ.get("DOCS_VERSION", "1.0")

# (file name, nav label, source, meta description). The first page is index.html.
PAGES = [
    ("index", "Start here", "READ-ME-FIRST.md",
     "Velaria quick start: what you need, the install in two lines, and the five things that go wrong with the one-line fix for each."),
    ("setup", "Setup guide", "setup-guide.md",
     "Install Velaria step by step: Quest developer mode, the Mac installer, DaVinci Resolve Studio settings, first picture and troubleshooting."),
    ("canvas-control", "Canvas Control", "howto-canvas-control.md",
     "How to place a footage window, a flat title or a 3D scene on a 360 or 180 shot with Velaria Canvas Control in DaVinci Resolve."),
    ("orient-360", "Orient 360 and patches", "howto-orient-360-and-patches.md",
     "How to re-level a stereo 360 shot with Velaria Orient 360 and work on LatLong Patcher paint-outs from inside a Quest headset."),
    ("trim", "Trim", "howto-trim.md",
     "How to make a brightness, contrast and colour trim pass from inside the headset with Velaria Trim on a Resolve adjustment clip."),
    ("180-and-fisheye", "180, 360 and fisheye", "howto-180-and-fisheye.md",
     "Velaria layout and projection settings: VR180, 360, mono and stereo, and the dual-fisheye preview with its lens presets."),
    ("headset", "Headset reference", "headset-reference.md",
     "Every Velaria View panel in the Quest headset: the strip, Placement, Look, Timeline, Scopes, the level grid and controller shortcuts."),
    ("known-limits", "Known limits", "known-limits.md",
     "What Velaria 1.0 does not do: supported Macs, Resolve versions and headsets, Blackmagic hardware, picture, sound and network limits."),
    ("support", "Support and logs", "support.md",
     "How to get help with Velaria, how to make the logs zip with send-logs.sh, and exactly what the zip contains."),
    ("uninstall", "Uninstall", "uninstall.md",
     "How to remove Velaria from the Mac and the Quest, what the uninstaller leaves behind, and how to switch back to a Blackmagic card."),
    ("changelog", "What changed", "changelog.md",
     "Velaria release notes, newest first: what each version of the Resolve to Quest preview tool added, changed or fixed."),
]

# Browser titles for pages whose own heading is too short to say what the page is (aim: 45 to 60 characters).
TITLES = {
    "index": "Velaria docs: DaVinci Resolve to Meta Quest, start here",
    "setup": "Velaria setup guide: Mac, Resolve Studio and Quest",
    "orient-360": "Orient 360 and patches in the headset | Velaria docs",
    "known-limits": "Known limits of Velaria 1.0 | Velaria docs",
    "support": "Velaria support and sending logs | Velaria docs",
    "uninstall": "Uninstall Velaria from Mac and Quest | Velaria docs",
    "changelog": "Velaria release notes: what changed | Velaria docs",
}

# Words that must never reach a public page: development machine names, internal project and card names, the old
# product name (allowed on the changelog page only, for the rename note), alpha wording, personal paths.
FORBIDDEN = re.compile(
    r"\bNYC\b|\bBruce\b|\bTony\b|KALYX TEST|kalyx-test|Salmon|Hay ?Bale|\bVine\b|/Users/|/Volumes/|/home/|~/VRLink"
    r"|cmd\.txt|test hook|\balpha\b|\btesters?\b", re.I)
OLD_NAME = re.compile(r"kalyx", re.I)

CSS = """
/* Tokens are the sales page's (site/index.html): one dark room, copper for what you act on, ice for status. */
:root {
  --ground: #15181d; --step: #1d222a; --step-2: #252b34; --well: #0d0f13;
  --ink: #f2f3f5; --ink-2: #b4b8bf; --rule: #ffffff1f;
  --copper: #e0a97a; --on-copper: #15181d; --ice: #8fc1d9;
  --display: "Saira Extra Condensed", "Saira", "Arial Narrow", system-ui, sans-serif;
  --text: "Saira", system-ui, -apple-system, "Segoe UI", sans-serif;
  --mono: ui-monospace, "SF Mono", Menlo, Consolas, monospace;
  --margin: clamp(16px, 3.2vw, 48px);
  color-scheme: dark;
}
* { box-sizing: border-box; }
html { -webkit-text-size-adjust: 100%; }
body { margin: 0; background: var(--ground); color: var(--ink); font-family: var(--text); font-size: 17px; line-height: 1.6; -webkit-font-smoothing: antialiased; }
a { color: var(--copper); text-underline-offset: 3px; }
.skip { position: absolute; left: -999px; top: 8px; background: var(--copper); color: var(--on-copper); padding: 8px 12px; border-radius: 8px; z-index: 5; }
.skip:focus { left: 8px; }
.frame { display: grid; grid-template-columns: 280px minmax(0, 1fr); min-height: 100vh; }
.rail { position: sticky; top: 0; align-self: start; height: 100vh; overflow: auto; padding: 28px 16px 28px var(--margin); border-right: 1px solid var(--rule); background: var(--step); }
.brand { font-family: var(--display); font-weight: 800; text-transform: uppercase; font-size: 32px; line-height: 1; letter-spacing: 0.02em; margin: 0; }
.brand a { color: var(--ink); text-decoration: none; }
.brand-sub { font-weight: 600; font-size: 12px; letter-spacing: 0.18em; text-transform: uppercase; color: var(--ink-2); margin: 8px 0 0; }
.pages { list-style: none; margin: 24px 0 0; padding: 0; display: flex; flex-direction: column; gap: 2px; }
.pages a { display: block; padding: 9px 12px; border-radius: 10px; color: var(--ink-2); text-decoration: none; font-weight: 600; }
.pages a:hover { color: var(--ink); background: var(--step-2); }
.pages a[aria-current="page"] { color: var(--on-copper); background: var(--copper); }
.toc { margin: 4px 0 10px 12px; padding: 0; list-style: none; font-size: 14px; }
.toc a { color: var(--ink-2); text-decoration: none; padding: 4px 12px; display: block; border-left: 2px solid var(--rule); }
.toc a:hover { color: var(--ink); border-left-color: var(--copper); }
.rail-foot { margin-top: 24px; font-size: 13px; color: var(--ink-2); line-height: 1.5; }
main { padding: 44px var(--margin) 96px; min-width: 0; }
.page { max-width: 74ch; margin: 0 auto; min-width: 0; }
.eyebrow { font-size: 12px; font-weight: 700; letter-spacing: 0.2em; text-transform: uppercase; color: var(--copper); margin: 0 0 10px; }
h1 { font-family: var(--display); font-weight: 800; text-transform: uppercase; font-size: clamp(40px, 6vw, 64px); line-height: 0.95; margin: 0 0 20px; text-wrap: balance; }
h2 { font-family: var(--display); font-weight: 800; text-transform: uppercase; font-size: 32px; line-height: 1; letter-spacing: 0.01em; margin: 48px 0 14px; padding-top: 22px; border-top: 1px solid var(--rule); text-wrap: balance; scroll-margin-top: 16px; }
h3 { font-size: 18px; margin: 28px 0 8px; }
ul, ol { padding-left: 22px; }
li { margin: 5px 0; }
strong { font-weight: 700; }
code { font-family: var(--mono); font-size: 0.86em; background: var(--step-2); padding: 1px 6px; border-radius: 6px; overflow-wrap: anywhere; }
pre { background: var(--well); border: 1px solid var(--rule); border-radius: 12px; padding: 14px 16px; overflow-x: auto; }
pre code { background: none; padding: 0; font-size: 14px; overflow-wrap: normal; }
.table-wrap { overflow-x: auto; margin: 14px 0; }
table { border-collapse: collapse; width: 100%; font-size: 15px; }
th, td { text-align: left; vertical-align: top; padding: 9px 12px 9px 0; border-bottom: 1px solid var(--rule); }
th { font-size: 12px; letter-spacing: 0.12em; text-transform: uppercase; color: var(--ink-2); font-weight: 700; }
img { max-width: 100%; height: auto; border-radius: 14px; border: 1px solid var(--rule); background: var(--ground); margin: 18px 0 6px; display: block; }
.next { display: flex; justify-content: space-between; gap: 16px; margin-top: 56px; padding-top: 22px; border-top: 1px solid var(--rule); font-weight: 600; }
.next a { text-decoration: none; }
.next a:hover { text-decoration: underline; }
.fine { margin-top: 28px; font-size: 13px; color: var(--ink-2); }
:focus-visible { outline: 2px solid var(--copper); outline-offset: 3px; }
@media (max-width: 880px) {
  .frame { grid-template-columns: minmax(0, 1fr); }
  .rail { position: static; height: auto; border-right: 0; border-bottom: 1px solid var(--rule); padding: 18px var(--margin); }
  .pages { flex-direction: row; flex-wrap: wrap; margin-top: 14px; }
  .pages a { padding: 7px 10px; font-size: 15px; }
  .toc, .rail-foot { display: none; }
  main { padding-top: 28px; padding-bottom: 64px; }
}
@media (prefers-reduced-motion: no-preference) { html { scroll-behavior: smooth; } }
"""

TRADEMARKS = ("DaVinci Resolve, DeckLink and UltraStudio are trademarks of Blackmagic Design Pty Ltd. Meta and Quest are "
              "trademarks of Meta Platforms, Inc. Mac and macOS are trademarks of Apple Inc. Canon is a trademark of "
              "Canon Inc. NDI is a trademark of Vizrt NDI AB. Velaria is an independent product and is not made or "
              "endorsed by any of them.")


def image_size(name):
    try:
        from PIL import Image
        with Image.open(REF_SRC / name) as im:
            return im.size
    except Exception:
        return None


def render(md_name):
    text = (SRC / md_name).read_text(encoding="utf-8")
    if SUPPORT_EMAIL:
        text = text.replace("[SUPPORT EMAIL]", f"[{SUPPORT_EMAIL}](mailto:{SUPPORT_EMAIL})")
    body = markdown.markdown(text, extensions=["tables", "fenced_code"])
    m = re.match(r"\s*<h1>(.*?)</h1>", body, re.S)   # the first H1 is the page title
    title = html.unescape(re.sub(r"<[^>]+>", "", m.group(1))) if m else md_name
    body = body[m.end():] if m else body
    seen = {}

    def anchor(mm):
        t = re.sub(r"<[^>]+>", "", mm.group(1))
        slug = re.sub(r"[^a-z0-9]+", "-", html.unescape(t).lower()).strip("-") or "section"
        seen[slug] = seen.get(slug, 0) + 1
        if seen[slug] > 1:
            slug = f"{slug}-{seen[slug]}"
        return f'<h2 id="{slug}">{mm.group(1)}</h2>'
    body = re.sub(r"<h2>(.*?)</h2>", anchor, body, flags=re.S)
    body = body.replace("<table>", '<div class="table-wrap"><table>').replace("</table>", "</table></div>")

    def img(mm):
        tag = mm.group(0)
        src = re.search(r'src="ref/([^"]+)"', tag)
        if not src:
            return tag
        size = image_size(src.group(1))
        extra = ' loading="lazy" decoding="async"' + (f' width="{size[0]}" height="{size[1]}"' if size else "")
        return tag.replace("<img ", "<img" + extra + " ", 1)
    body = re.sub(r"<img [^>]*>", img, body)
    # Markdown wraps a lone image in a paragraph; a figure-less image needs no <p>
    body = re.sub(r"<p>(<img [^>]*>)</p>", r"\1", body)
    return title, body


def page_html(i, rendered):
    pid, label, _, desc = PAGES[i]
    title, body = rendered[i]
    heads = re.findall(r'<h2 id="([^"]+)">(.*?)</h2>', body, flags=re.S)
    nav = []
    for j, (qid, qlabel, _, _) in enumerate(PAGES):
        cur = ' aria-current="page"' if j == i else ""
        nav.append(f'<li><a href="{qid}.html"{cur}>{html.escape(qlabel)}</a>')
        if j == i and heads:
            items = "".join(f'<li><a href="#{hid}">{re.sub(r"<[^>]+>", "", ht)}</a></li>' for hid, ht in heads)
            nav.append(f'<ul class="toc">{items}</ul>')
        nav.append("</li>")
    prev_link = f'<a href="{PAGES[i - 1][0]}.html" rel="prev">&larr; {html.escape(PAGES[i - 1][1])}</a>' if i > 0 else "<span></span>"
    next_link = f'<a href="{PAGES[i + 1][0]}.html" rel="next">{html.escape(PAGES[i + 1][1])} &rarr;</a>' if i + 1 < len(PAGES) else "<span></span>"
    doc_title = TITLES.get(pid) or f"{title} | Velaria docs"
    ld = json.dumps({"@context": "https://schema.org", "@type": "TechArticle", "headline": title, "description": desc,
                     "inLanguage": "en-CA", "isPartOf": {"@type": "WebSite", "name": "Velaria docs"},
                     "about": {"@type": "SoftwareApplication", "name": "Velaria", "operatingSystem": "macOS 13 or later; Meta Quest",
                               "applicationCategory": "MultimediaApplication"},
                     **({"url": f"{DOCS_URL}/{'' if pid == 'index' else pid + '.html'}"} if DOCS_URL else {})},
                    ensure_ascii=False).replace("</", "<\\/")
    canonical = f'<link rel="canonical" href="{DOCS_URL}/{"" if pid == "index" else pid + ".html"}">\n' if DOCS_URL else ""
    return f"""<!doctype html>
<html lang="en-CA">
<head>
<meta charset="utf-8">
<meta name="viewport" content="width=device-width, initial-scale=1">
<title>{html.escape(doc_title)}</title>
<meta name="description" content="{html.escape(desc, quote=True)}">
{canonical}<meta property="og:type" content="article">
<meta property="og:title" content="{html.escape(doc_title, quote=True)}">
<meta property="og:description" content="{html.escape(desc, quote=True)}">
<meta name="twitter:card" content="summary">
<meta name="theme-color" content="#15181d">
<script type="application/ld+json">{ld}</script>
<link rel="preconnect" href="https://fonts.googleapis.com">
<link rel="preconnect" href="https://fonts.gstatic.com" crossorigin>
<link rel="stylesheet" href="https://fonts.googleapis.com/css2?family=Saira+Extra+Condensed:wght@800&family=Saira:wght@400;600;700&display=swap">
<style>{CSS}</style>
</head>
<body>
<a class="skip" href="#content">Skip to the page</a>
<div class="frame">
  <nav class="rail" aria-label="Velaria docs">
    <p class="brand"><a href="{html.escape(SITE_URL, quote=True)}">Velaria</a></p>
    <p class="brand-sub">Docs · {html.escape(VERSION)}</p>
    <ul class="pages">{"".join(nav)}</ul>
    <p class="rail-foot">For a Mac with Apple silicon, DaVinci Resolve Studio and a Meta Quest. The pictures of the headset panels are drawn by the app's own code.</p>
  </nav>
  <main id="content">
    <article class="page">
      <p class="eyebrow">Velaria · {html.escape(label)}</p>
      <h1>{html.escape(title)}</h1>
      {body}
      <p class="next">{prev_link}{next_link}</p>
      <p class="fine">{TRADEMARKS}</p>
    </article>
  </main>
</div>
</body>
</html>
"""


def scrub(pid, page):
    """Returns the problems found in one built page (visible text and attributes alike)."""
    text = re.sub(r"<style>.*?</style>", "", page, flags=re.S)   # JSON-LD stays in: it is public text too
    problems = [f"{pid}.html: forbidden text '{m.group(0)}'" for m in FORBIDDEN.finditer(text)]
    old = OLD_NAME.findall(text)
    if old and pid != "changelog":
        problems.append(f"{pid}.html: the old product name appears outside the changelog")
    if pid == "changelog" and len(old) > 1:
        problems.append("changelog.html: the old product name appears more than once (only the rename note may use it)")
    return problems


def main():
    rendered = [render(md) for _, _, md, _ in PAGES]
    pages = {PAGES[i][0]: page_html(i, rendered) for i in range(len(PAGES))}
    problems = [p for pid, page in pages.items() for p in scrub(pid, page)]
    # every picture a page asks for must exist
    for pid, page in pages.items():
        for name in re.findall(r'src="ref/([^"]+)"', page):
            if not (REF_SRC / name).exists():
                problems.append(f"{pid}.html: missing picture ref/{name}")
    if problems:
        print("\n".join(problems), file=sys.stderr)
        sys.exit("build_docs_site: not written, fix the problems above")
    if OUT.exists():
        shutil.rmtree(OUT)
    (OUT / "ref").mkdir(parents=True)
    used = sorted({n for page in pages.values() for n in re.findall(r'src="ref/([^"]+)"', page)})
    for name in used:
        shutil.copy(REF_SRC / name, OUT / "ref" / name)
    for pid, page in pages.items():
        (OUT / f"{pid}.html").write_text(page, encoding="utf-8")
    if DOCS_URL:
        urls = "".join(f"<url><loc>{DOCS_URL}/{'' if pid == 'index' else pid + '.html'}</loc></url>" for pid in pages)
        (OUT / "sitemap.xml").write_text(
            f'<?xml version="1.0" encoding="UTF-8"?><urlset xmlns="http://www.sitemaps.org/schemas/sitemap/0.9">{urls}</urlset>\n',
            encoding="utf-8")
    print(f"wrote {len(pages)} pages and {len(used)} pictures to {OUT}")
    left = sorted(pid for pid, page in pages.items() if "[SUPPORT EMAIL]" in page)
    if left:
        print("WARNING: [SUPPORT EMAIL] is still a placeholder on: " + ", ".join(left) + ". Set SUPPORT_EMAIL before publishing.")
    if SITE_URL.startswith("#"):
        print("WARNING: SITE_URL is not set; the Velaria wordmark links to a placeholder.")
    if not DOCS_URL:
        print("WARNING: DOCS_URL is not set; no canonical tags or sitemap were written.")


if __name__ == "__main__":
    main()
