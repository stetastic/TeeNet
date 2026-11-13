from pathlib import Path
root=Path(__file__).resolve().parents[1]
source=(root/'main/app.c').read_text(encoding='utf-8')
start=source.index('static bool save_energy(void) {')
end=source.index('static bool sdm_meter(void)',start)
(root/'tests/energy_save_actual.inc').write_text(source[start:end],encoding='utf-8')
