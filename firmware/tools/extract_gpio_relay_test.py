"""Test the production GPIO driver with simulated pin levels, not real outputs."""
from pathlib import Path
root=Path(__file__).resolve().parents[1]
source=(root/'main/app.c').read_text(encoding='utf-8')
start=source.index('static void relay_set(')
end=source.index('/* Both external inputs',start)
(root/'tests/gpio_relay_actual.inc').write_text(source[start:end],encoding='utf-8')
