import json
import shutil
import subprocess
from pathlib import Path

ROOT = Path(__file__).parent
GALLERY = ROOT / 'gallery'
concepts = json.loads((GALLERY / 'concepts.json').read_text())
ORIGINALS = ROOT / 'originals'
ORIGINALS.mkdir(exist_ok=True)
for c in concepts:
    c['image'] = c['image'].replace('.png', '.webp')
for entry in json.loads((ROOT / 'generated-paths.json').read_text()):
    original = ORIGINALS / Path(entry['target']).name
    if not original.exists():
        shutil.copy2(entry['source'], original)
    target = GALLERY / entry['target'].replace('.png', '.webp')
    if not target.exists():
        subprocess.run(['convert', str(original), '-define', 'webp:lossless=true', str(target)], check=True)
for old in (GALLERY / 'assets').glob('*.png'):
    shutil.move(str(old), str(ORIGINALS / old.name))
(GALLERY / 'concepts.json').write_text(json.dumps(concepts, indent=2))

cards = []
for c in concepts:
    n = c['level']
    cards.append(f'''<article class="card" data-chapter="{c['chapter']}">
<button class="plate" data-level="{n}" aria-label="Enlarge level {n}: {c['name']}"><img src="{c['image']}" alt="{c['name']}: landscape and portrait concept mockups" loading="lazy" width="1536" height="1024"><span class="expand">View mockup ↗</span></button>
<div class="card-body"><div class="card-top"><span class="number">{n:02}</span><span class="chapter">{c['chapter']}</span><span class="intensity">{c['intensity']:g} / 10</span></div>
<h2>{c['name']}</h2><p>{c['concept'].rsplit(' Intensity', 1)[0]}</p>
<div class="palette">'''+''.join(f'<span><i style="background:{c[k]}"></i>{k.title()} <code>{c[k]}</code></span>' for k in ('base','secondary','accent'))+f'''</div>
<details><summary>Proposed motion and transition</summary><p>{c['motion']}</p></details></div></article>''')

swatches = ''.join(f'''<a class="swatch" href="#level-{c['level']}" data-jump="{c['level']}" title="{c['name']} — {c['base']}"><i style="background:{c['base']}"></i><span>{c['level']:02}</span><b>{c['name']}</b><code>{c['base']}</code></a>''' for c in concepts)

html = '''<!doctype html>
<html lang="en"><head><meta charset="utf-8"><meta name="viewport" content="width=device-width,initial-scale=1"><meta name="color-scheme" content="dark"><title>STACKFALL / Background studies</title>
<style>
:root{--bg:#0b0e14;--panel:#11151e;--line:#29303f;--text:#e8ecf6;--muted:#98a3b9;--accent:#77d7db}*{box-sizing:border-box}body{margin:0;background:var(--bg);color:var(--text);font:15px/1.6 system-ui,-apple-system,sans-serif}button,a,summary{touch-action:manipulation}button{font:inherit;color:inherit;cursor:pointer}a{color:var(--accent)}.wrap{max-width:1560px;margin:auto;padding:0 36px}.masthead{padding:24px 0;border-bottom:1px solid var(--line);display:flex;justify-content:space-between;gap:20px;font:12px/1.5 ui-monospace,monospace;letter-spacing:.12em}.masthead span:last-child{color:var(--muted)}header{padding:64px 0 38px;max-width:940px}.eyebrow{color:var(--accent);font:12px ui-monospace,monospace;letter-spacing:.16em}h1{font-size:clamp(38px,6vw,78px);line-height:1.08;letter-spacing:-.055em;font-weight:600;margin:22px 0}header p{font-size:18px;color:var(--muted);max-width:720px}header .note{font-size:13px;max-width:820px;border-left:2px solid var(--line);padding-left:16px}.arc{display:grid;grid-template-columns:repeat(5,1fr);gap:1px;background:var(--line);border:1px solid var(--line);margin:0 0 32px}.arc div{background:var(--panel);padding:17px 20px}.arc b{display:block;font-size:16px}.arc span{color:var(--muted);font:12px ui-monospace,monospace}.toolbar{position:sticky;top:0;z-index:2;background:#0b0ef4;background:rgba(11,14,20,.97);padding:16px 0;display:flex;gap:14px;justify-content:space-between;border-block:1px solid var(--line);align-items:center}.filters{display:flex;flex-wrap:wrap;gap:6px}.filters button{background:transparent;border:1px solid var(--line);padding:7px 14px;border-radius:3px;font-size:13px}.filters button[aria-pressed=true]{background:var(--text);color:var(--bg);border-color:var(--text)}.tools{display:flex;align-items:center;gap:14px;white-space:nowrap;font-size:12px;color:var(--muted)}.tools label{cursor:pointer}.grid{display:grid;grid-template-columns:repeat(2,minmax(0,1fr));gap:28px;margin:30px 0 60px}.card{background:var(--panel);border:1px solid var(--line);scroll-margin-top:110px;min-width:0}.card[hidden]{display:none}.plate{display:block;position:relative;border:0;background:#080a10;padding:0;width:100%;text-align:left}.plate img{display:block;width:100%;height:auto;aspect-ratio:3/2;object-fit:contain}.plate:hover .expand,.plate:focus-visible .expand{opacity:1}.expand{position:absolute;right:12px;bottom:12px;background:#1b2230;border:1px solid #4e5874;padding:5px 10px;font-size:12px;opacity:0}.card-body{padding:22px 24px}.card-top{display:flex;align-items:center;gap:12px;font:12px ui-monospace,monospace}.number{color:var(--accent);font-size:18px}.chapter{color:var(--muted);text-transform:uppercase;letter-spacing:.12em}.intensity{margin-left:auto;color:var(--muted)}h2{font-size:25px;font-weight:500;letter-spacing:-.02em;margin:10px 0}.card-body p{font-size:13px;color:var(--muted);margin:0 0 18px}.palette{display:flex;gap:10px 16px;flex-wrap:wrap;font-size:10px;color:var(--muted);padding-top:10px;border-top:1px solid var(--line)}.palette span{display:flex;align-items:center;gap:5px}.palette i{display:inline-block;width:14px;height:14px;border:1px solid #485269}.palette code{font-size:10px;color:var(--text)}details{border-top:1px solid var(--line);margin-top:16px;padding-top:10px}summary{font-size:12px;cursor:pointer;color:var(--muted)}details p{padding-top:10px}.summary{border-top:1px solid var(--line);padding-top:28px;margin-bottom:50px}.summary h2{margin-top:0}.summary p{color:var(--muted);font-size:13px}.swatches{display:grid;grid-template-columns:repeat(10,minmax(0,1fr));gap:12px}.swatch{text-decoration:none;color:var(--text);display:grid;gap:4px;font-size:10px}.swatch i{display:block;height:42px;border:1px solid #485269}.swatch span{color:var(--muted);font:11px ui-monospace,monospace}.swatch b{font-weight:400;white-space:normal}.swatch code{color:var(--muted);font-size:10px}footer{display:flex;justify-content:space-between;gap:24px;padding:25px 0 40px;border-top:1px solid var(--line);color:var(--muted);font-size:12px}footer a{margin-right:16px}body.gray .plate img,body.gray #large-image{filter:grayscale(1)}dialog{padding:0;border:1px solid var(--line);background:var(--bg);color:var(--text);max-width:96vw;width:1440px;max-height:96dvh}dialog::backdrop{background:#000d}.dialog-top{display:flex;align-items:center;gap:12px;padding:12px 18px;border-bottom:1px solid var(--line)}.dialog-top b{font-size:14px;font-weight:500;margin-right:auto}.dialog-top button{background:var(--panel);border:1px solid var(--line);padding:4px 12px}.dialog-image{display:flex;justify-content:center;background:#080a10;overflow:auto}#large-image{display:block;max-width:100%;max-height:78dvh;object-fit:contain;width:auto;height:auto}.dialog-foot{padding:10px 18px;color:var(--muted);font-size:12px}:focus-visible{outline:2px solid var(--accent);outline-offset:3px}@media(max-width:900px){.wrap{padding:0 20px}.tools{flex-wrap:wrap;justify-content:flex-end}.swatches{grid-template-columns:repeat(5,minmax(0,1fr))}.arc div{padding:12px}.grid{gap:18px}.card-body{padding:16px}}@media(max-width:650px){.wrap{padding:0 14px}.masthead{font-size:10px;letter-spacing:.04em}header{padding-top:36px}header p{font-size:16px}.arc div{padding:9px 6px}.arc b{font-size:12px}.arc span{font-size:10px}.toolbar{align-items:flex-start;flex-direction:column;gap:8px}.filters{gap:4px}.filters button{padding:6px 9px;font-size:12px}.tools{width:100%;justify-content:space-between}.grid{grid-template-columns:1fr;margin-top:18px}.swatches{gap:10px}.swatch b{font-size:9px}.swatch code{font-size:9px}.dialog-top{gap:6px;padding:10px}.dialog-top button{padding:4px 8px}footer{flex-direction:column}}@media(prefers-reduced-motion:reduce){*{scroll-behavior:auto!important}}
</style></head><body><div class="wrap">
<div class="masthead"><span>STACKFALL / ART DIRECTION</span><span>BACKGROUND STUDIES · 05 NOV 2026</span></div>
<header><div class="eyebrow">20 LEVELS / TWO ORIENTATIONS</div><h1>A quiet start.<br>A controlled climb.</h1><p>Twenty background concepts, from open dark space to dense edge patterns. Each plate pairs a landscape view with a portrait view.</p><p class="note">Generated with imagegen using the game screenshot as a reference. These are concept mockups. Game geometry, colors, counts, and pixel edges need verification during implementation. Motion notes describe proposed behavior; the images are static.</p></header>
<div class="arc" aria-label="The five chapters"><div><b>Still</b><span>01–04</span></div><div><b>Stir</b><span>05–08</span></div><div><b>Build</b><span>09–12</span></div><div><b>Pressure</b><span>13–16</span></div><div><b>Peak</b><span>17–20</span></div></div>
<nav class="toolbar" aria-label="Gallery controls"><div class="filters"><button data-filter="All" aria-pressed="true">All levels</button><button data-filter="Still" aria-pressed="false">Still</button><button data-filter="Stir" aria-pressed="false">Stir</button><button data-filter="Build" aria-pressed="false">Build</button><button data-filter="Pressure" aria-pressed="false">Pressure</button><button data-filter="Peak" aria-pressed="false">Peak</button></div><div class="tools"><span id="count" aria-live="polite">20 concepts</span><label><input type="checkbox" id="gray"> Grayscale review</label></div></nav>
<main class="grid" id="gallery">CARDS</main>
<section class="summary"><h2>The palette arc</h2><p>All twenty base colors in level order. Select a swatch to open its mockup. The listed colors are design targets.</p><div class="swatches">SWATCHES</div></section>
<footer><span>Concept review / Based on the background design brief dated 2026-11-05.</span><span><a href="prompts.md">Generation prompts</a><a href="summary.md">Design summary</a><a href="concepts.json">Concept data</a></span></footer>
</div>
<dialog id="viewer" aria-labelledby="viewer-title"><div class="dialog-top"><b id="viewer-title"></b><button id="previous" aria-label="Previous concept">←</button><button id="next" aria-label="Next concept">→</button><button id="close" aria-label="Close enlarged view">Close ×</button></div><div class="dialog-image"><img id="large-image" alt=""></div><div class="dialog-foot">Landscape + portrait / Use the arrow keys to compare. Press Escape to close.</div></dialog>
<script>
const concepts=CONCEPT_DATA;
const viewer=document.querySelector('#viewer'),large=document.querySelector('#large-image');let selected=0,opener=null;
function show(level){selected=(level+19)%20;const c=concepts[selected];large.src=c.image;large.alt=c.name+': landscape and portrait concept mockups';document.querySelector('#viewer-title').textContent=String(c.level).padStart(2,'0')+' / '+c.name+' / '+c.chapter;if(!viewer.open){opener=document.activeElement;viewer.showModal();}}
document.querySelectorAll('[data-level]').forEach(b=>b.addEventListener('click',()=>show(Number(b.dataset.level))));
document.querySelectorAll('[data-jump]').forEach(a=>a.addEventListener('click',e=>{e.preventDefault();show(Number(a.dataset.jump));}));
document.querySelectorAll('[data-filter]').forEach(b=>b.addEventListener('click',()=>{document.querySelectorAll('[data-filter]').forEach(x=>x.setAttribute('aria-pressed',String(x===b)));let n=0;document.querySelectorAll('.card').forEach(c=>{c.hidden=b.dataset.filter!=='All'&&c.dataset.chapter!==b.dataset.filter;if(!c.hidden)n++;});document.querySelector('#count').textContent=n+' concepts';}));
document.querySelector('#gray').addEventListener('change',e=>document.body.classList.toggle('gray',e.target.checked));
document.querySelector('#previous').addEventListener('click',()=>show(selected));document.querySelector('#next').addEventListener('click',()=>show(selected+2));document.querySelector('#close').addEventListener('click',()=>viewer.close());
viewer.addEventListener('close',()=>{if(opener)opener.focus();});viewer.addEventListener('click',e=>{if(e.target===viewer){const r=viewer.getBoundingClientRect();if(e.clientX<r.left||e.clientX>r.right||e.clientY<r.top||e.clientY>r.bottom)viewer.close();}});viewer.addEventListener('keydown',e=>{if(e.key==='ArrowRight'){e.preventDefault();show(selected+2);}if(e.key==='ArrowLeft'){e.preventDefault();show(selected);}});
</script></body></html>'''
html = html.replace('CARDS', '\n'.join(cards)).replace('SWATCHES',swatches).replace('CONCEPT_DATA',json.dumps(concepts))
html = html.replace('large.src=c.image;', '''large.src=document.querySelector('[data-level="'+c.level+'"] img').src;''')
html = html.replace('function show(level){', 'function show(level,trigger=document.activeElement){').replace('opener=document.activeElement;', 'opener=trigger;')
html = html.replace('show(Number(b.dataset.level))', 'show(Number(b.dataset.level),b)').replace('show(Number(a.dataset.jump));', 'show(Number(a.dataset.jump),a);')
(GALLERY / 'index.html').write_text(html)
summary = '''# STACKFALL background mockups

Date: 2026-11-05

Twenty imagegen concept plates show both landscape and portrait views.
Open index.html for the gallery. Select a chapter to filter the mockups.
Select a plate to enlarge it. Arrow keys change the enlarged concept.
Use Grayscale review to inspect the entire plate without color.

These are visual concepts, not exact game captures or renderer assets.
Image generation approximates the board geometry, HUD, palette, and pixel edges.
The supplied brief defines the design targets. No layout mask was available.
The screenshot at docs/images/gameplay.png provides the visual reference.
Motion notes are proposals. No animation or playtest is included.
The listed intensity values are design intentions, not measured results.
Exact coordinates, color limits, accent coverage, element budgets, and readability
need verification in the game renderer before acceptance.

## Concepts

| Level | Name | Chapter | Base | Secondary | Accent | Intensity | Proposed motion and transition |
|---|---|---|---|---|---|---|---|
'''
for c in concepts:
    summary += f"| {c['level']} | {c['name']} | {c['chapter']} | {c['base']} | {c['secondary']} | {c['accent']} | {c['intensity']} | {c['motion']} |\n"
(GALLERY / 'summary.md').write_text(summary)
print(json.dumps({'plates_present':len(list((GALLERY / 'assets').glob('*.webp'))),'html_bytes':len(html.encode()),'total_bytes':sum(p.stat().st_size for p in GALLERY.rglob('*') if p.is_file())}))
