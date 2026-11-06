import base64
import json
import shutil
from pathlib import Path

ROOT = Path(__file__).parent
gallery = ROOT / 'gallery'
handoff = ROOT / 'handoff'
handoff.mkdir(exist_ok=True)
html = (gallery / 'index.html').read_text()
concepts = json.loads((gallery / 'concepts.json').read_text())
for c in concepts:
    image = gallery / c['image']
    if not image.exists():
        raise RuntimeError(f'Missing plate for level {c["level"]}')
    data = base64.b64encode(image.read_bytes()).decode('ascii')
    html = html.replace(f'src="{c["image"]}"', f'src="data:image/webp;base64,{data}"')
(handoff / 'index.html').write_text(html)
for name in ('summary.md', 'prompts.md', 'concepts.json'):
    shutil.copy2(gallery / name, handoff / name)
size = sum(p.stat().st_size for p in handoff.iterdir() if p.is_file())
print(json.dumps({'files':len(list(handoff.iterdir())), 'total_bytes':size,'inline_plates':html.count('src="data:image/webp;base64,')}))
if size >= 20971520:
    raise RuntimeError('The self-contained gallery exceeds the anonymous drop limit. Use the gallery folder with same-drop assets instead.')
