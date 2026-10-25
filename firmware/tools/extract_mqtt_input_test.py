"""Test the current embedded MQTT routing verbatim, with host SDK scaffolding."""
from pathlib import Path
root = Path(__file__).resolve().parents[1]
source = (root / 'main/app.c').read_text(encoding='utf-8')
start = source.index('static bool mqtt_input(')
end = source.index('/* Called only by status_task', start)
runtime=(root/'main/heatpump_runtime.inc').read_text(encoding='utf-8')
fragment = runtime.split('static bool heatpump_poll_once(')[0] + source[start:end]
assert 'static void mqtt_event(' in fragment
(root / 'tests/mqtt_input_actual.inc').write_text(fragment, encoding='utf-8')
