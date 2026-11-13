"""Keep the test on the actual control mutation and revision-check implementation."""
from pathlib import Path

root = Path(__file__).resolve().parents[1]
source = (root / 'main/app.c').read_text(encoding='utf-8')
start = source.index('static bool apply_control(cJSON *o) {')
end = source.index('\nstatic bool mqtt_input(', start)
(root / 'tests/control_revision_actual.inc').write_text(source[start:end], encoding='utf-8')
