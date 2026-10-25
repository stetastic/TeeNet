"""Install the heat-pump experiment in a demo build, never in ESP assets."""
from pathlib import Path
import re
import shutil

TOOLS = Path(__file__).resolve().parent


def install_heatpump_demo(destination):
    destination = Path(destination)
    index = destination / 'index.html'
    html = index.read_text(encoding='utf-8')
    marker = '<label class="check-label"><input name="battery_protect" type="checkbox">Hausakku</label>'
    assert html.count(marker) == 1
    if 'name="heatpump_enabled"' not in html:
        html = html.replace(marker, marker + '<label class="check-label"><input name="heatpump_enabled" type="checkbox">Wärmepumpe anzeigen</label>', 1)
    marker = '<details class="panel settings-group" id="battery-settings"'
    assert html.count(marker) == 1
    settings = (TOOLS / 'heatpump-demo-settings.html').read_text(encoding='utf-8')
    if 'id="heatpump-settings"' in html:
        html, count = re.subn(r'<details\b[^>]*id="heatpump-settings"[^>]*>.*?</details>', lambda _: settings, html, count=1, flags=re.S)
        assert count == 1
    else:
        html = html.replace(marker, settings + marker, 1)
    script = re.search(r'<script defer src="/dashboard\.js[^" ]*"></script>', html)
    assert script, 'Dashboard script missing'
    assets = ('heatpump-demo-api.js', 'heatpump-demo-energy.js', 'heatpump-demo-ui.js',
              'energy-total-demo-ui.js', 'consumer-details-demo.js')
    html = html[:script.start()] + ''.join(f'<script defer src="/{name}"></script>' for name in assets) + html[script.start():]
    html = html.replace('</head>', '<link rel="stylesheet" href="/heatpump-demo.css"><link rel="stylesheet" href="/energy-total-demo.css"><link rel="stylesheet" href="/consumer-details-demo.css"></head>', 1)
    index.write_text(html, encoding='utf-8')
    for name in (*assets, 'heatpump-demo.css', 'energy-total-demo.css', 'consumer-details-demo.css'):
        shutil.copy2(TOOLS / name, destination / name)
    for name in ('guide.html', 'help.html'):
        page = destination / name
        if not page.exists():
            continue
        manual = page.read_text(encoding='utf-8')
        marker = '<details id="energieanzeige">'
        if marker not in manual or 'id="waermepumpe-demo"' in manual or 'id="waermepumpe"' in manual:
            continue
        note = ('<details id="waermepumpe-demo"><summary>Wärmepumpe · Demo-Vorschau</summary>'
                '<p>Diese Erweiterung ist nur in der Demo enthalten. Unter Funktionen einschalten, '
                'danach Shelly-Zähler oder MQTT als Quelle wählen. Alle Werte und Verbindungen sind simuliert.</p>'
                '<p>In der Grafik zeigt das Haus den übrigen Verbrauch <b>ohne Wallbox und Wärmepumpe</b>. '
                'Beide Verbraucher werden separat abgezogen und angezeigt. Der Hausanschluss bleibt der '
                'unveränderte Netzbezug bzw. die Einspeisung. Ohne gültige Wärmepumpen-Messung ist der Rest unbekannt.</p>'
                '<p>Die kW-Werte bleiben sichtbar; weitere Werte öffnen per Bildklick. '
                'Die aktuelle ESP-Firmware enthält noch keine Wärmepumpen-Anbindung.</p></details>')
        page.write_text(manual.replace(marker, note + marker, 1), encoding='utf-8')
