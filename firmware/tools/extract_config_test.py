"""Exercise the actual ESP config parser and validation up to the flash write."""
from pathlib import Path

root = Path(__file__).resolve().parents[1]
source = (root / 'main/app.c').read_text(encoding='utf-8')
table_start = source.index('typedef struct { const char *name; size_t offset,size; char type; } field_t;')
table_end = source.index('static cJSON *config_json(', table_start)
handler_start = source.index('static esp_err_t config_apply(')
handler_end = source.index('    if(meter_changed) {\n        nvs_handle_t', source.index('    if(save_config(&next)!=ESP_OK)', handler_start))
fragment = source[table_start:table_end] + source[handler_start:handler_end]
fragment += '    phase_config_saving=false; prepared_settings=next; prepared_count++; req->status=200; return ESP_OK;\n}\n'
(root / 'tests/config_apply_actual.inc').write_text(fragment, encoding='utf-8')
