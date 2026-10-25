from pathlib import Path
root=Path(__file__).resolve().parents[1]
source=(root/'main/app.c').read_text(encoding='utf-8')
start=source.index('static esp_err_t theme_get(')
end=source.index('static esp_err_t status_handler(',start)
(root/'tests/appearance_actual.inc').write_text(source[start:end],encoding='utf-8')
