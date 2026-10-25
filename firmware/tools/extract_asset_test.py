from pathlib import Path
root=Path(__file__).resolve().parents[1]
text=(root/'main/app.c').read_text(encoding='utf-8')
start=text.index('static esp_err_t asset_send(');end=text.index('static SemaphoreHandle_t image_slots;',start)
(root/'tests/asset_actual.inc').write_text(text[start:end],encoding='utf-8')
