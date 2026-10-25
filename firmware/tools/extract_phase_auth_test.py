"""Test the real expert endpoint's authorization before parameter handling."""
from pathlib import Path

root = Path(__file__).resolve().parents[1]
source = (root / 'main/expert_http.inc').read_text(encoding='utf-8')
start = source.index('static esp_err_t expert_post(')
end = source.index('    LOCK();bool stopped=', start)
fragment = source[start:end] + '    cJSON_Delete(o); return httpd_resp_send_err(req,400,"Unsupported test action");\n}\n'
(root / 'tests/phase_auth_actual.inc').write_text(fragment, encoding='utf-8')
