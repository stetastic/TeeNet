"""Host regression for the production energy/session reset and NVS snapshot."""
from pathlib import Path

root = Path(__file__).resolve().parents[1]
source = (root / 'main/app.c').read_text(encoding='utf-8')
snapshot = source[source.index('static bool save_energy(void)'):source.index('static bool sdm_meter(void)')]
handlers = source[source.index('static esp_err_t energy_reset_handler('):source.index('static esp_err_t reboot_handler(')]
(root / 'tests/storage_reset_generated.inc').write_text(
    '/* Generated from main/app.c; do not edit. */\n' + snapshot + handlers,
    encoding='utf-8',
)
