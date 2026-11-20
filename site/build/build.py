"""Builds the Velaria sales page: the 12 Nov sales page (site/index.html on claude/week-nov-9) with the
19-20 Nov changes (two tiers, dome, sound, notes, coexistence) and the comp's hero, round-trip section and
'Try the controls' playground. Run: python3 build/build.py  ->  index.html"""
import json, re, html, pathlib
ROOT = pathlib.Path(__file__).resolve().parent.parent
HERE = pathlib.Path(__file__).resolve().parent
SALES = (HERE / 'sales-12nov.html').read_text()   # the 12 Nov sales page (fa9f42c)
COMP = (HERE / 'comp-2nov.html').read_text()      # the design comp, artifact v15

def between(s, a, b, inc_a=True, inc_b=False):
    i = s.index(a); j = s.index(b, i + len(a))
    return s[i if inc_a else i + len(a): j + (len(b) if inc_b else 0)]

# ---------------- CSS ----------------
css = between(SALES, '<style>', '</style>', inc_a=False)
hero_css = between(COMP, '/* ---------- Hero:', '/* ---------- Problem:')
prob_css = between(COMP, '/* ---------- Problem:', '/* ---------- How it works')
play_css = between(COMP, '/* ---------- Try the controls', 'input[type="range"]')
css = css.replace(between(css, '/* ---------- Hero ----------', '/* ---------- Problem:'), hero_css)
css = css.replace(between(css, '/* ---------- Problem:', '/* ---------- How it works'), prob_css)
css = css.replace(between(css, '/* ---------- Try it: level the horizon', 'input[type="range"]'), play_css)
css = css.replace('.wordmark { font-family: var(--display); font-weight: 800; font-size: 30px; letter-spacing: 0.06em; text-decoration: none; }',
  '.wordmark { display: inline-flex; align-items: center; gap: 12px; font-family: "Red Hat Display", var(--text); font-weight: 600; font-size: 24px; letter-spacing: 0.16em; text-decoration: none; }\n.wordmark svg { width: 30px; height: 30px; flex: none; }')
css = css.replace('.facts { display: grid; grid-template-columns: repeat(5, minmax(0, 1fr));', '.facts { display: grid; grid-template-columns: repeat(4, minmax(0, 1fr));')
css += '''
/* ---------- Two versions ---------- */
.tag { display: inline-flex; align-items: center; min-height: 28px; padding: 0 12px; border-radius: 999px; font-size: 14px; font-weight: 600; background: var(--step-2); color: var(--copper); margin-bottom: 18px; }
.tag--ice { color: var(--ice); background: color-mix(in oklab, var(--ice) 16%, var(--step)); }
.see { padding-block: var(--section) 0; }
.choose { display: grid; grid-template-columns: repeat(2, minmax(0, 1fr)); gap: var(--gutter); margin-top: var(--gutter); }
.choose .tier { background: var(--ground); }
.choose .tier--mid { background: var(--step-2); }
.choose .price { font-size: clamp(64px, 6vw, 96px); color: var(--ink); }
.choose .price small { color: var(--ink-2); }
.choose .button { justify-self: start; margin-top: 8px; }
.button--ghost { background: transparent; color: var(--ink); box-shadow: inset 0 0 0 2px var(--copper); }
@media (hover: hover) and (pointer: fine) { .button--ghost:hover { background: color-mix(in oklab, var(--copper) 18%, transparent); } }
.checkout { margin: 18px 0 0; font-size: 15px; color: var(--ink-2); max-width: 46em; }
@media (max-width: 720px) { .choose { grid-template-columns: 1fr; } }
'''

# ---------------- Copy ----------------
FAQ = [
 ("View or Canvas: which one do I need?",
  ["Velaria View is the monitor. You see your live Resolve timeline in the headset in every format, with sound, scopes, a level grid and notes that land as Resolve markers. If you only need to check your cut where your audience will see it, View is enough.",
   "Velaria Canvas is everything in View, plus the controls that change the picture from inside the headset: move titles and footage windows, set their depth, level and re-orient the shot, set keyframes and do a trim pass. It includes the Velaria effects for Resolve that those controls drive."]),
 ("What do I get when I buy Velaria?",
  ["One download for your Mac, with the Velaria output that DaVinci Resolve Studio plays to, the Velaria app for your Quest, the Velaria Sound plug-in for Fairlight, an installer and uninstaller, and the setup guide. Velaria Canvas also includes the Velaria effects for Resolve: Canvas Control, Orient 360 and 180, Stereo and Trim.",
   "Both are one-time purchases, with no subscription and no license key."]),
 ("Which Mac, which Resolve and which headset do I need?",
  ["A Mac with Apple silicon running macOS 13 or newer, DaVinci Resolve Studio 19, 20 or 21, and a Meta Quest 3. Quest 3S, Quest 2 and Quest Pro run the app, but Quest 3 is the headset we test on.",
   "You also need a USB-C data cable for the install. After that you can work over the cable or over Wi-Fi."]),
 ("Does it work with the free version of DaVinci Resolve?",
  ["No. The free version has no video output for Velaria to connect to, so Velaria needs DaVinci Resolve Studio."]),
 ("Why does my Quest need developer mode?",
  ["The Velaria app is not in the Meta Horizon Store, so the installer loads it onto your headset over the cable, and Meta only allows that when developer mode is on. Turning it on means creating a free Meta developer account, and Meta may take a few days to verify it, so it is worth starting first.",
   "If you would rather not turn developer mode on, this version of Velaria isn't for you."]),
 ("macOS says it can't verify the developer. Is that expected?",
  ["Yes. Velaria is not signed or notarized by Apple. On most Macs the installer runs from Terminal without a warning. If macOS does stop it, the requirements box above gives the exact message and the fix: System Settings, Privacy & Security, Open Anyway, then run the installer again."]),
 ("I own a DeckLink or UltraStudio. Can I keep using it?",
  ["Yes. Velaria and your Blackmagic device can both appear in Resolve's Video and Audio I/O, and you switch between them there. Resolve asks for a restart each time you switch, which is how it behaves with any two Blackmagic devices. We have tested this with a DeckLink Mini Monitor 4K; the setup guide has the install step.",
   "If a Desktop Video update replaces Velaria's driver, the headset tells you, and running the installer again puts it back. On macOS 26, if your card doesn't show up at all, approve Blackmagic's driver extension under System Settings, General, Login Items & Extensions. That one is on Blackmagic's side, and it catches people out whether Velaria is installed or not."]),
 ("Does it work with Premiere Pro, Final Cut Pro or on Windows?",
  ["Not today. Velaria is for DaVinci Resolve Studio on a Mac. A Windows version is planned for early 2027 if enough editors ask for it, so if you want it, write and tell us. Premiere Pro and Final Cut Pro are not planned."]),
 ("Does it work with Pico, Apple Vision Pro or other headsets?",
  ["Velaria is for Meta Quest today. Pico headsets are next, soon after launch. Apple Vision Pro is not planned: DaVinci Resolve Studio has its own monitoring for that headset."]),
 ("Which formats and resolutions can I preview?",
  ["360 in mono or over/under stereo; 180 as side-by-side VR180 or as dual fisheye with lens presets from 144 to 220 degrees; and dome, from a standard domemaster.",
   "Pictures up to 5760 pixels wide reach the headset at full size (a 5760 x 2880 VR180 timeline, for example); wider pictures are scaled down to 5760. Resolve's monitoring picture is no taller than 4320 pixels, so a square 360 timeline such as 5760 x 5760 is monitored at 3840 x 3840 while you work. Velaria sets that output scaling in Resolve for you while the headset is connected, and puts your own setting back when you disconnect."]),
 ("Can I preview a dome show?",
  ["Yes, in both View and Canvas. Open a standard domemaster timeline (square, 180 degree fisheye, front at the bottom of the frame) and choose the Dome projection in the headset. You sit in a modeled theater: choose small, medium or large, a screen tilt from level to 30 degrees, and one of five seats, then fine-tune the tilt a degree at a time.",
   "It is a preview for composition, pacing and comfort, not a substitute for a check in the real theater."]),
 ("Can I hear my mix in the headset?",
  ["Yes. Put the Velaria Sound plug-in on a Fairlight bus and that bus plays through the Quest in step with the picture, with no patching. A stereo bus plays as stereo. A first-order ambisonic bus turns with your head, so a sound placed behind you stays behind you as you look around.",
   "It is a spatialized review, not a mix monitor: mix on your studio system."]),
 ("Can I finish a color grade in the headset?",
  ["No. A Quest is not a reference display. Velaria gives you scopes measured on the Mac, and Canvas adds a trim pass, so you can check and adjust how the film sits on the device your audience will wear. Grade on your reference monitor."]),
 ("Cable or Wi-Fi?",
  ["The USB-C cable is the default and the steadier choice. Wi-Fi works too, on a fast network you trust, with the Mac and the headset on the same network. Either way the app finds your Mac on its own."]),
 ("How is this different from a headset app that shows a video stream from my Mac?",
  ["Those apps show you a picture, usually through an NDI plug-in and an extra node in your Resolve tree. Velaria appears in Resolve as a video output, with nothing added to your node tree, and is built to stay in step with Resolve's viewer, with your mix in sync. With Canvas you can also change the project from inside the shot, with undo."]),
 ("How many computers can I install it on? Are updates free?",
  ["One purchase covers one person, on the Macs and headsets that person uses. A second editor needs a second copy. Every 1.x update is free. A later major version may be a paid upgrade."]),
 ("Does Velaria send any data anywhere?",
  ["There is no account, no analytics and no telemetry. Over the cable, the picture and sound go from your Mac to your headset and nowhere else. Over Wi-Fi the stream is not encrypted, so use a network you trust. The privacy statement has the details."]),
 ("What if it doesn't work for me?",
  ["Write to us within 14 days of buying and we will refund you, no questions asked. Support is by email. If you run the log script that comes with Velaria and attach the file, we can usually see what went wrong."]),
]
esc = lambda s: html.escape(s, quote=False)
faq_html = '\n'.join(f'      <details{" open" if i == 0 else ""}><summary>{esc(q)}</summary><div>' + ''.join(f'<p>{esc(p)}</p>' for p in ps) + '</div></details>' for i, (q, ps) in enumerate(FAQ))
faq_ld = json.dumps({"@context": "https://schema.org", "@type": "FAQPage", "mainEntity": [
    {"@type": "Question", "name": q, "acceptedAnswer": {"@type": "Answer", "text": " ".join(ps)}} for q, ps in FAQ]}, indent=1, ensure_ascii=False)

RING = '<svg viewBox="0 0 100 100" aria-hidden="true"><g transform="rotate(-20 50 50)"><ellipse cx="50" cy="50" rx="34" ry="42" fill="#f2f3f5"/><ellipse cx="55" cy="49.5" rx="25" ry="33" fill="#e0a97a"/><ellipse cx="51.5" cy="50.5" rx="22" ry="31" fill="#15181d"/></g></svg>'

head = f'''<!doctype html>
<html lang="en-US">
<head>
<meta charset="utf-8">
<meta name="viewport" content="width=device-width,initial-scale=1,viewport-fit=cover">
<title>Velaria: live 360, VR180 and dome preview from DaVinci Resolve to Meta Quest</title>
<meta name="description" content="See your live DaVinci Resolve timeline in a Meta Quest, in stereo, for 360, VR180 and dome. Place titles and level the horizon from inside the shot. Mac, one-time.">
<!--
  DRAFT for Nelson's approval (20 Nov 2026). Not published on the H&A site.
  Prices are working numbers (View 39, Canvas 89 USD), not confirmed. Before publishing, replace:
    #BUY-URL-VIEW  #BUY-URL-CANVAS   the checkout links (set once, in VELARIA)
    #DOCS-URL  #EULA-URL  #PRIVACY-URL  #NOTICES-URL   the setup guide and legal pages
    SUPPORT-EMAIL    the support address (set once, in VELARIA.supportEmail)
  Built by build/build.py from the 12 Nov sales page and the design comp's hero, round trip and playground.
-->
<script>
/* ONE PLACE TO CHANGE PRICES, THE BUY LINKS AND THE SUPPORT ADDRESS. Working numbers, Nelson to confirm. */
window.VELARIA = {{
  priceView: 39,
  priceCanvas: 89,
  currency: 'USD',
  buyUrlView: '#BUY-URL-VIEW',
  buyUrlCanvas: '#BUY-URL-CANVAS',
  supportEmail: 'SUPPORT-EMAIL',
  refundDays: 14
}};
document.documentElement.classList.add('js');
</script>
<link rel="preconnect" href="https://fonts.googleapis.com">
<link rel="preconnect" href="https://fonts.gstatic.com" crossorigin>
<link rel="stylesheet" href="https://fonts.googleapis.com/css2?family=Red+Hat+Display:wght@600&family=Red+Hat+Text:wght@500;600;700&family=Saira+Extra+Condensed:wght@700;800&family=Saira:ital,wght@0,400;0,500;0,600;0,700;1,300&display=swap">
<style>{css}</style>
<script type="application/ld+json">
{faq_ld}
</script>
</head>
'''

hero = f'''  <section class="hero" aria-labelledby="hero-title">
    <div class="wrap">
      <div class="hero__grid">
        <div class="hero__copy">
          <h1 id="hero-title" class="display"><span class="line"><span style="--i:0">Stop exporting</span></span><span class="line"><span style="--i:1">to see your cut.</span></span></h1>
          <p class="lead" data-load style="--d:420ms">Velaria puts your live DaVinci Resolve Studio timeline inside a Meta Quest as you work, in stereo and in step with Resolve's viewer, for 360, VR180 and dome films. With Velaria Canvas you can move a title, set its depth and level the horizon without taking the headset off.</p>
          <div class="hero__act" data-load style="--d:500ms">
            <a class="button" href="#buy">Choose your version</a>
            <a class="textlink" href="#how">See how it works</a>
          </div>
        </div>
        <figure class="hv" data-load style="--d:300ms; margin: 0;" aria-label="Your Resolve timeline on a Mac, shown live in a Meta Quest">
          <div class="mac">
            <p class="hv__tag"><b>1</b> Your timeline, in Resolve on your Mac</p>
            <div class="mac__win" aria-hidden="true">
              <div class="mac__bar"><i></i><i></i><i></i><span>Strange Worlds · 360 stereo</span></div>
              <div class="mac__viewer"></div>
              <div class="tl">
                <div class="tl__row" data-t="V2"><div class="clip clip--title" style="left: 44%; width: 30%;">STRANGE WORLDS</div></div>
                <div class="tl__row" data-t="V1"><div class="clip" style="left: 0; width: 30%; background-image: url(img/t0.jpg);"></div><div class="clip" style="left: 31%; width: 40%; background-image: url(img/t1.jpg);"></div><div class="clip" style="left: 72%; width: 28%; background-image: url(img/t2.jpg);"></div></div>
                <div class="tl__row" data-t="A1"><div class="clip clip--audio" style="left: 0; width: 100%;"></div></div>
                <div class="tl__head"></div>
              </div>
            </div>
          </div>
          <svg class="link" viewBox="0 0 300 160" aria-hidden="true">
            <path id="hv-path" d="M8 10 C 120 10, 170 140, 292 150"/>
            <circle r="5"><animateMotion dur="2.4s" repeatCount="indefinite"><mpath href="#hv-path"/></animateMotion></circle>
            <text x="150" y="58">live</text>
          </svg>
          <div class="quest">
            <div class="quest__view">
              <span class="quest__status">USB · Resolve · live · sound</span>
              <img class="quest__panel" src="img/panel.png" alt="" width="900" height="840">
            </div>
            <p class="hv__tag" style="margin: 10px 0 0 6px;"><b>2</b> The same frame, live in your headset, with your mix</p>
          </div>
        </figure>
      </div>
      <dl class="facts" data-load style="--d:580ms">
        <div><dt>Works with</dt><dd>DaVinci Resolve Studio on a Mac with Apple silicon</dd></div>
        <div><dt>Formats</dt><dd>360, VR180, fisheye 180 and dome, mono or stereo</dd></div>
        <div><dt>Headset</dt><dd>Meta Quest 3, over USB-C or Wi-Fi</dd></div>
        <div><dt>Price</dt><dd>From <span data-price="view">$39</span>, once. No subscription.</dd></div>
      </dl>
    </div>
  </section>
'''

problem = '''  <section class="problem wrap" id="problem" aria-labelledby="problem-title">
    <div class="pain">
      <h2 id="problem-title" class="head" data-reveal>Every tweak is a round trip.</h2>
      <ol class="trip" data-reveal aria-label="What checking one change looks like today">
        <li>Export</li><li>Copy it to the headset</li><li>Put it on</li><li class="bad">The title's in your face</li><li>Take it off</li><li>Nudge it</li><li class="again">Again</li>
      </ol>
      <p class="count" data-reveal>A flat monitor can't show depth or tell you whether the horizon is level, so you guess, then check. <span id="trips" aria-hidden="true">Round trip <b>1</b>.</span></p>
      <p class="pain__turn" data-reveal>Or skip the trip.</p>
    </div>
  </section>
'''

how = '''  <section class="how wrap" id="how" aria-labelledby="how-title">
    <div class="sechead">
      <h2 id="how-title" class="head" data-reveal>Install. Headset on. Press play.</h2>
      <p class="lead muted" data-reveal>Velaria sits between the editor you already use and the headset you already own. Nothing changes in how you cut.</p>
    </div>
    <div class="chain" data-reveal>
      <div class="chain__step">
        <p class="chain__where">On your Mac</p>
        <h3 class="sub">Install Velaria</h3>
        <p>Resolve sees Velaria as one more video output.</p>
        <ul>
          <li>One installer, run once</li>
          <li>In Resolve's Video and Audio I/O, pick Velaria (Quest)</li>
          <li>A DeckLink or UltraStudio can stay beside it</li>
        </ul>
      </div>
      <svg class="chain__arrow" viewBox="0 0 44 20" aria-hidden="true"><path d="M2 10h36M30 3l8 7-8 7" fill="none" stroke="currentColor" stroke-width="2.5" stroke-linecap="round" stroke-linejoin="round"/></svg>
      <div class="chain__step">
        <p class="chain__where">On your Quest</p>
        <h3 class="sub">Put the headset on</h3>
        <p>The Velaria app finds your Mac on its own.</p>
        <ul>
          <li>USB-C cable, or Wi-Fi on a network you trust</li>
          <li>No pairing codes and no addresses to type</li>
        </ul>
      </div>
      <svg class="chain__arrow" viewBox="0 0 44 20" aria-hidden="true"><path d="M2 10h36M30 3l8 7-8 7" fill="none" stroke="currentColor" stroke-width="2.5" stroke-linecap="round" stroke-linejoin="round"/></svg>
      <div class="chain__step">
        <p class="chain__where">In your timeline</p>
        <h3 class="sub">Press play</h3>
        <p>The shot under your playhead is the shot you're standing in.</p>
        <ul>
          <li>In step with Resolve's viewer as you scrub</li>
          <li>Stereo depth you can actually see</li>
          <li>Your mix in the headset, in sync</li>
        </ul>
      </div>
    </div>
  </section>
'''

depth = between(SALES, '  <section class="depth"', '  <section class="features', inc_b=False)

see = '''  <section class="see wrap" id="view" aria-labelledby="view-title">
    <div class="sechead">
      <div><p class="tag tag--ice" data-reveal>Velaria View and Canvas</p>
      <h2 id="view-title" class="head" data-reveal>See it, hear it, note it.</h2></div>
      <p class="lead muted" data-reveal>Everything you need to review a cut from inside it, in both versions: the live picture in every format, your mix, and notes that land in your Resolve project.</p>
    </div>
    <ul class="flist">
      <li data-reveal><h3>Your mix, in sync</h3><p>Put the Velaria Sound plug-in on a Fairlight bus and the mix plays in the headset in step with the picture, with no patching. An ambisonic mix turns with your head. A spatialized review, not a mix monitor.</p></li>
      <li data-reveal><h3>Notes that land in Resolve</h3><p>Drop a note from the headset and it becomes a marker at the playhead, colored by kind: Editing, Sound, Color, Graphics, Immersive, or Love this.</p></li>
      <li data-reveal><h3>Grab a still</h3><p>Save the frame you're looking at to a Velaria Stills folder beside your media, without Resolve changing pages.</p></li>
      <li data-reveal><h3>Hide a layer</h3><p>Switch a title or a footage window off to see the plate behind it, and bring it back with one tap.</p></li>
      <li data-reveal><h3>Scopes in the headset</h3><p>Waveform, RGB parade, vectorscope and histogram, measured on your Mac from the frame Resolve sends, before any compression.</p></li>
      <li data-reveal><h3>A true horizon</h3><p>A level grid over the picture, with degree marks, so a tilt of a degree shows up before your audience feels it.</p></li>
      <li data-reveal><h3>It tells you what's wrong</h3><p>When a Resolve setting is off, the headset names it and gives the fix in Resolve's own menu words.</p></li>
      <li data-reveal><h3>Scrub, play, park</h3><p>The headset follows the playhead as you work and holds the frame when you stop, so you can look around a parked shot.</p></li>
    </ul>
  </section>
'''

feat = between(SALES, '  <section class="features', '  <section class="level"')
feat = feat.replace('''      <h2 id="features-title" class="head" data-reveal>Fix it while you're in there.</h2>''',
 '''      <div><p class="tag" data-reveal>Velaria Canvas</p>
      <h2 id="features-title" class="head" data-reveal>Fix it while you're in there.</h2></div>''')
new_list = '''      <ul class="flist">
        <li data-reveal><h3>Place titles and footage</h3><p>Point at an overlay in the scene to select it, then drag it where it belongs. Pan, tilt, roll, distance and scale each have their own control.</p></li>
        <li data-reveal><h3>Set stereo depth by eye</h3><p>Push a title back or pull it forward and watch it settle against the scene, so nothing ends up floating in the viewer's face. Set the convergence of a whole stereo shot the same way.</p></li>
        <li data-reveal><h3>Level and re-orient 360 and 180</h3><p>Re-level a shot against the grid, or turn the sphere so the action starts in front.</p></li>
        <li data-reveal><h3>Keyframes and undo</h3><p>A keyframe on any control, marks on the timeline deck, and undo that goes straight to your Resolve project.</p></li>
        <li data-reveal><h3>Always know what you're moving</h3><p>Overlays, patches and the whole picture are separate layers, each named, with a lock so the plate stays put.</p></li>
        <li data-reveal><h3>A trim pass on the delivery device</h3><p>Exposure, lift, gamma, gain, contrast, saturation, temperature and tint on a layer above your grade. A trim for the headset your audience will wear, not a final grade.</p></li>
        <li data-reveal><h3>180 with the same tools</h3><p>The Velaria 180 effects let Canvas Control, Orient and Trim work on a VR180 timeline just as they do on 360.</p></li>
        <li data-reveal><h3>Copy a placement</h3><p>Copy where one card sits and paste it onto another, so a series of titles lands in the same place.</p></li>
      </ul>'''
feat = re.sub(r'      <ul class="flist">.*?</ul>', new_list, feat, flags=re.S)

play = between(COMP, '  <section class="play" id="try"', '  <section class="tiers wrap" id="tiers"')
play = play.replace("This is the panel from the headset, working. You're standing in the middle of a dome. Pick the title or the footage window on the strip, then turn the ring, pull the throttle, set the window depth and level the picture. The scopes read the frame as you go, and the trim pass shows what's coming in the beta. Drag the view to look around.",
  "These are Velaria Canvas's panels from the headset, working. Pick the title or the footage window on the strip, then turn the ring, pull the throttle, set the window depth and level the picture. The scopes read the frame as you go, and the trim pass sits on top of it all. Drag the view to look around.")

formats = '''  <section class="tiers wrap" id="formats" aria-labelledby="formats-title" style="padding-top: var(--section)">
    <div class="sechead">
      <h2 id="formats-title" class="head" data-reveal>360, 180 and dome. In both versions.</h2>
      <p class="lead muted" data-reveal>Velaria shows each format the way your audience will see it, mono or stereo.</p>
    </div>
    <div class="tiers__grid">
      <article class="tier" data-reveal>
        <p class="tier__name">360</p>
        <h3>The sphere</h3>
        <p>The whole sphere, around you.</p>
        <ul>
          <li>Mono, or stereo over/under</li>
          <li>Level grid; with Canvas, re-level and re-orient</li>
          <li>Up to 5760 pixels wide at full size</li>
          <li>Square timelines just work: Velaria sets Resolve's output scaling while you watch, and puts it back after</li>
        </ul>
      </article>
      <article class="tier" data-reveal>
        <p class="tier__name">180</p>
        <h3>VR180</h3>
        <p>Straight from the timeline.</p>
        <ul>
          <li>Side-by-side VR180, at full size</li>
          <li>Dual fisheye, unwrapped in the headset</li>
          <li>Lens presets from 144 to 220 degrees, with fine trim</li>
        </ul>
      </article>
      <article class="tier tier--mid" data-reveal>
        <p class="tier__name">Dome</p>
        <h3>Fulldome</h3>
        <p>Sit in your dome show before the dome is booked.</p>
        <ul>
          <li>A standard domemaster, overhead, from a seat in a modeled theater</li>
          <li>Small, medium or large, with a screen tilt from level to 30 degrees</li>
          <li>Five seats: the sweet spot, front row, rear row, left and right</li>
          <li>Fine tilt, a degree at a time</li>
        </ul>
        <p class="tier__when">A preview for composition, pacing and comfort, not a substitute for a check in the real theater.</p>
      </article>
    </div>
  </section>
'''

fit = '''  <section class="isnt wrap" id="fit" aria-labelledby="fit-title">
    <div class="sechead">
      <h2 id="fit-title" class="head" data-reveal>What it is. What it isn't.</h2>
      <p class="lead muted" data-reveal>Velaria is a small tool that does one job well. Here is the plain list, so you know before you buy.</p>
    </div>
    <div class="isnt__grid">
      <article class="tier tier--mid" data-reveal>
        <p class="tier__name">Velaria is</p>
        <ul>
          <li>A live headset monitor for DaVinci Resolve Studio on a Mac</li>
          <li>With Canvas, a way to place overlays, set depth and level a shot from inside it</li>
          <li>Built for 360, 180 and dome, mono or stereo</li>
          <li>A one-time purchase, with every 1.x update included</li>
          <li>Private: no account, no analytics, no telemetry</li>
        </ul>
      </article>
      <article class="tier" data-reveal>
        <p class="tier__name">Velaria isn't</p>
        <ul>
          <li class="no">For Premiere Pro, Final Cut Pro or the free version of Resolve</li>
          <li class="no">On Windows or Pico yet. Both are coming, below</li>
          <li class="no">For Apple Vision Pro</li>
          <li class="no">A Meta Horizon Store app. You load it with developer mode</li>
          <li class="no">Signed by Apple. macOS may ask you to approve it</li>
          <li class="no">A reference display, a stitcher or an encoder</li>
          <li class="no">A mix monitor. Headset sound is a spatialized review</li>
        </ul>
      </article>
    </div>
    <div class="tiers__note" data-reveal>
      <p><b>Coming next:</b> Pico headsets, soon after launch. Then Windows, planned for early 2027 if enough editors ask for it.</p>
      <a class="textlink" data-mail="Velaria for Windows">Tell us you want Windows</a>
    </div>
  </section>
'''

buy = '''  <section class="buy" id="buy" aria-labelledby="buy-title">
    <div class="buy__grid wrap">
      <div class="buy__copy">
        <h2 id="buy-title" class="head">Buy it once.</h2>
        <p class="lead muted">Two versions. Check the five things on the right first, then pick the one that fits how you work.</p>
        <ul class="buy__terms">
          <li>No subscription and no license key</li>
          <li>Every 1.x update included</li>
          <li>One person, on the Macs and headsets you use</li>
          <li>Full refund within <span data-v="refundDays">14</span> days, no questions asked</li>
          <li>Sales tax or VAT is added at checkout where it applies</li>
        </ul>
      </div>
      <div>
        <div class="req" id="requirements">
          <h3 class="sub">Check these five things first</h3>
          <ol>
            <li><b>A Mac with Apple silicon, and DaVinci Resolve Studio.</b><span>macOS 13 or newer, and Resolve Studio 19, 20 or 21. The free version of Resolve will not work.</span></li>
            <li><b>A Meta Quest 3 with developer mode on.</b><span>Developer mode needs a free Meta developer account, and Meta may take a few days to verify it. Quest 3S, Quest 2 and Quest Pro run the app, but we test on Quest 3.</span></li>
            <li><b>Velaria is not signed by Apple.</b><span>You install it from Terminal with one command, and on most Macs it runs without a warning. If macOS says <q>Apple could not verify [file name] is free of malware that may harm your Mac or compromise your privacy</q> (macOS 15 and newer) or <q>[file name] cannot be opened because the developer cannot be verified</q> (macOS 13 and 14), click Done, open System Settings, go to Privacy &amp; Security, scroll to Security and click Open Anyway. Then run the installer again.</span></li>
            <li><b>If you use a DeckLink or UltraStudio, read the setup guide first.</b><span>Velaria and your Blackmagic device can both appear in Resolve's Video and Audio I/O, and Resolve restarts when you switch between them. We have tested this with a DeckLink Mini Monitor 4K.</span></li>
            <li><b>Support is by email, and updates come when they're ready.</b><span>There is no phone support and no guaranteed response time. If it doesn't work on your setup, you get your money back.</span></li>
          </ol>
        </div>
        <div class="choose">
          <article class="tier">
            <p class="tier__name">Velaria View</p>
            <p class="price"><span data-price="view">$39</span><small>one-time</small></p>
            <p>See your cut where your audience will.</p>
            <ul>
              <li>Your live timeline in the headset, in step with Resolve's viewer</li>
              <li>360, VR180, fisheye and dome, with theater tilt and seats</li>
              <li>Your mix in sync, ambisonic mixes head-tracked</li>
              <li>Scopes, level grid, notes as markers, stills</li>
            </ul>
            <a class="button button--ghost" data-buy="view" href="#BUY-URL-VIEW">Buy View</a>
          </article>
          <article class="tier tier--mid">
            <p class="tier__name">Velaria Canvas</p>
            <p class="price"><span data-price="canvas">$89</span><small>one-time</small></p>
            <p>Edit your immersive film from inside it.</p>
            <ul>
              <li>Everything in View</li>
              <li>Move titles and footage windows, and set their depth</li>
              <li>Level and re-orient 360 and 180, and set convergence</li>
              <li>Keyframes with Resolve's undo, and a trim pass</li>
              <li>The Velaria effects for Resolve</li>
            </ul>
            <a class="button" data-buy="canvas" href="#BUY-URL-CANVAS">Buy Canvas</a>
          </article>
        </div>
        <p class="checkout">Secure checkout by Lemon Squeezy. Your download link arrives by email. By buying you agree to the <a class="textlink" href="#EULA-URL">license agreement</a>.</p>
      </div>
    </div>
  </section>
'''

faq = f'''  <section class="faq wrap" id="faq" aria-labelledby="faq-title">
    <div class="sechead">
      <h2 id="faq-title" class="head" data-reveal>Questions, answered straight.</h2>
      <p class="lead muted" data-reveal>If yours isn't here, write to us before you buy. We would rather answer a question than process a refund.</p>
    </div>
    <div class="faq__list">
{faq_html}
    </div>
  </section>
'''
support = between(SALES, '  <section class="support', '</main>')
footer = between(SALES, '<footer', '</footer>', inc_b=True)
footer = footer.replace('Edit your immersive film from inside it. Made by Hammer &amp; Anvil.', 'Stop exporting to see your cut. Made by Hammer &amp; Anvil.')
footer = footer.replace('<a class="textlink" href="#how">How it works</a><a class="textlink" href="#formats">Formats</a>', '<a class="textlink" href="#how">How it works</a><a class="textlink" href="#try">Try it</a><a class="textlink" href="#formats">Formats</a>')
footer = footer.replace('>Licence agreement<', '>License agreement<')

bar = f'''<header class="bar wrap">
  <a class="wordmark" href="#top" aria-label="Velaria, back to top">{RING}VELARIA</a>
  <nav aria-label="Main">
    <a class="navlink" href="#how">How it works</a>
    <a class="navlink" href="#try">Try it</a>
    <a class="navlink" href="#formats">Formats</a>
    <a class="navlink" href="#faq">Questions</a>
    <a class="button" href="#buy">Buy Velaria</a>
  </nav>
</header>
'''

# ---------------- Script ----------------
play_js = between(COMP, '  /* Try the controls: a canvas view inside the dome', '  /* The round-trip counter')
play_js = play_js.replace('/* ---------- Trim: the colour trim pass, planned for the beta. Drawn in the Look panel\'s own controls. ---------- */',
                          '/* ---------- Trim: the trim pass, drawn in the panel\'s own controls. ---------- */')
play_js = play_js.replace('aria-label="Trim panel, planned for the beta"', 'aria-label="Trim panel"')
play_js = re.sub(r"\s*s \+= `<rect x=\"730\" y=\"18\" width=\"104\" height=\"30\" rx=\"15\" fill=\"#2b3a45\"/>` \+ tc\(782, 39, 'BETA', 15, 700, '#8fc1d9', ';letter-spacing:3px'\);", '', play_js)
play_js = play_js.replace("'A trim pass on top of your grade, on the device your audience uses. Planned for the beta.'", "'A trim pass on top of your grade, on the device your audience uses.'")
play_js = play_js.replace('colourist', 'colorist')
trips_js = between(COMP, '  /* The round-trip counter', '  /* Alpha form')
sales_js = between(SALES, '<script>\n(() => {', '</script>\n\n</body>', inc_a=False)
sales_js = sales_js.replace('''  const money = (n) => '$' + n + (V.currency === 'USD' ? '' : ' ' + V.currency);
  document.querySelectorAll('[data-price]').forEach((el) => { el.textContent = money(V.priceCore) + (el.closest('.price') ? '' : ' ' + V.currency); });''',
'''  const money = (n) => '$' + n + (V.currency === 'USD' ? '' : ' ' + V.currency);
  const priceOf = { view: V.priceView, canvas: V.priceCanvas };
  document.querySelectorAll('[data-price]').forEach((el) => { el.textContent = money(priceOf[el.dataset.price]); });''')
sales_js = sales_js.replace("document.querySelectorAll('[data-buy]').forEach((el) => { el.href = V.buyUrl; });",
  "document.querySelectorAll('[data-buy]').forEach((el) => { el.href = el.dataset.buy === 'view' ? V.buyUrlView : V.buyUrlCanvas; });")
sales_js = sales_js.replace("offers: { '@type': 'Offer', price: String(V.priceCore), priceCurrency: V.currency, availability: 'https://schema.org/InStock' } });",
  "offers: [{ '@type': 'Offer', name: 'Velaria View', price: String(V.priceView), priceCurrency: V.currency }, { '@type': 'Offer', name: 'Velaria Canvas', price: String(V.priceCanvas), priceCurrency: V.currency }] });")
sales_js = re.sub(r"  /\* Level the horizon: the one live control \*/.*?roll\.addEventListener\('input', setRoll\); setRoll\(\);\n", play_js + trips_js, sales_js, flags=re.S)
assert 'priceCore' not in sales_js and 'setRoll' not in sales_js

page = (head + '<body>\n\n' + bar + '\n<main id="top">\n' + hero + '\n' + problem + '\n' + how + '\n' + depth + see + '\n' + feat + play + '\n' + formats + '\n' + fit + '\n' + buy + '\n' + faq + '\n' + support + '</main>\n\n' + footer + '\n\n'
        + '<script src="https://cdnjs.cloudflare.com/ajax/libs/gsap/3.12.5/gsap.min.js" integrity="sha512-7eHRwcbYkK4d9g/6tD/mhkf++eoTHwpNM9woBxtPUBWm67zeAfFC+HrdoE2GanKeocly/VxeLvIqwvCdk7qScg==" crossorigin="anonymous" referrerpolicy="no-referrer"></script>\n'
        + '<script src="https://cdnjs.cloudflare.com/ajax/libs/gsap/3.12.5/ScrollTrigger.min.js" integrity="sha512-onMTRKJBKz8M1TnqqDuGBlowlH0ohFzMXYRNebz+yOcc5TQr/zAKsthzhuv0hiyUKEiQEQXEynnXCvNTOk50dg==" crossorigin="anonymous" referrerpolicy="no-referrer"></script>\n'
        + '<script>\n(() => {' + sales_js + '</script>\n\n</body>\n</html>\n')
for bad in ['—', '–', 'beta', 'Beta', 'BETA', 'alpha', 'Theatre', 'colour', 'Colour', 'licence', 'Licence', 'priceCore']:
    hits = [m.start() for m in re.finditer(re.escape(bad), page)]
    if hits: print('WARN', repr(bad), len(hits), [page[max(0, h - 60):h + 40].replace('\n', ' ') for h in hits[:3]])
(ROOT / 'index.html').write_text(page)
print('wrote', len(page), 'bytes')
