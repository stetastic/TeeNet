from pathlib import Path
root=Path(__file__).resolve().parents[1]
text=(root/'main/ota_http.inc').read_text(encoding='utf-8')
start=text.index('static bool ota_install_allowed_locked(')
end=text.index('static esp_err_t ota_handler(',start)
(root/'tests/ota_gate_actual.inc').write_text(text[start:end],encoding='utf-8')
