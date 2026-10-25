"""Test the application's real feedback dispatch, without ESP hardware."""
from pathlib import Path

root = Path(__file__).resolve().parents[1]
source = (root / 'main/app.c').read_text(encoding='utf-8')
start = source.index('static uint8_t phase_observed(void) {')
end = source.index('static bool phase_can_unlock_locked', start)
(root / 'tests/phase_observed_actual.inc').write_text(source[start:end], encoding='utf-8')
