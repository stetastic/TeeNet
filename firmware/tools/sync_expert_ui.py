"""Embed the maintained expert UI without regenerating unrelated dashboard code."""
from pathlib import Path

root = Path(__file__).resolve().parents[1]
path = root / 'main/dashboard.js'
source = path.read_text(encoding='utf-8')
start = source.index('/* Password stays in this page')
end = source.index(' window.teenetExpert={updateWrites:update};update();', start)
end = source.index('})();', end) + 5
source = source[:start] + (root / 'tools/expert_ui.js').read_text(encoding='utf-8').rstrip() + source[end:]
path.write_text(source, encoding='utf-8')
