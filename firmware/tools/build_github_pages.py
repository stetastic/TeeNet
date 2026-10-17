"""Export the synthetic demo for the /TeeNet/ GitHub Pages project path."""
from pathlib import Path
import json,re,shutil
root=Path(__file__).resolve().parents[1]
source=root/'preview'
repo=root.parent/'github-release-1.11'
dest=repo/'docs'
dest.mkdir(exist_ok=True)
base='/TeeNet'
def rewrite(text):
    text=text.replace('https://teennet-demo.stetastic.chatgpt.site', 'https://stetastic.github.io/TeeNet')
    # API calls remain root-relative: all demo APIs are synthetic, intercepted in the browser.
    text=re.sub(r'([\"\'`])/(?!/|TeeNet(?:/|[\"\'`])|api(?:/|[\"\'`]))',lambda m:m[1]+base+'/',text)
    text=re.sub(r'url\(/(?!/|TeeNet/)', 'url('+base+'/', text)
    return text
for path in source.rglob('*'):
    if not path.is_file():continue
    relative=path.relative_to(source)
    if relative.parts[0] not in ('help-images',) and len(relative.parts)>1:continue
    if len(relative.parts)==1 and path.suffix not in ('.html','.css','.js','.json','.svg','.png','.jpg','.md'):continue
    target=dest/relative;target.parent.mkdir(parents=True,exist_ok=True)
    if path.suffix in ('.html','.css','.js','.json','.svg','.md'):target.write_text(rewrite(path.read_text(encoding='utf-8')),encoding='utf-8')
    else:shutil.copy2(path,target)
for route,filename in [('guide','guide.html'),('help','help.html'),('downloads','downloads.html')]:
    directory=dest/route;directory.mkdir(exist_ok=True)
    (directory/'index.html').write_bytes((dest/filename).read_bytes())
downloads=root.parent/'teennet-demo/dist/downloads'
manifest=json.loads((downloads/'release.json').read_text())
out=dest/'downloads';out.mkdir(exist_ok=True)
names={'release.json','SHA256SUMS.txt','web-install.json'}
names.update(f'TeeNet-{manifest["version"]}-{suffix}web-install.json' for suffix in ('','andreas-testboard-'))
names.update(item['name'] for item in manifest['files'] if item['name'].endswith('factory.bin'))
for name in names:shutil.copy2(downloads/name,out/name)
(dest/'.nojekyll').write_text('')
# README links remain valid when the former demo host is retired.
readme=repo/'README.md';text=readme.read_text(encoding='utf-8')
text=re.sub(r'^# TeeNet \d+\.\d+', '# TeeNet '+manifest['version'],text)
text=text.replace('https://teennet-demo.stetastic.chatgpt.site','https://stetastic.github.io/TeeNet')
if 'Freiwillige Gerätestatistik' not in text:
    text=text.replace('## Hinweis','## Freiwillige Gerätestatistik\n\nMit dem aktuellen Build von Version 1.21 kann TeeNet nach ausdrücklicher Bestätigung eine zufällige Kennung und die Firmwareversion an den Projektzähler senden. Die Teilnahme ist bei der Einrichtung vorausgewählt und jederzeit abschaltbar. Keine Lade-, WLAN- oder Standortdaten. Die Demo sendet keine Lebenszeichen. Details stehen in der Anleitung unter Gerätestatistik.\n\n## Hinweis')
readme.write_text(text,encoding='utf-8')
print(json.dumps({'pages':str(dest),'version':manifest['version'],'url':'https://stetastic.github.io/TeeNet/'}))
