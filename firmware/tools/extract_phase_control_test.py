"""Exercise the application's real charge gates and idle-resume conditions."""
from pathlib import Path

root = Path(__file__).resolve().parents[1]
source = (root / 'main/app.c').read_text(encoding='utf-8')
pv_start = source.index('static bool pv_permitted_locked(int64_t now) {')
pv_end = source.index('static bool house_currents_ready_locked', pv_start)
target_start = source.index('static float unlatched_target_locked(int64_t now) {')
target_end = source.index('static float target_locked', target_start)
resume_start = source.index('        if(phase_idle_inhibit && next.enabled &&')
resume_end = source.index('\n\n', resume_start)
fragment = source[pv_start:pv_end] + source[target_start:target_end]
fragment += 'static void idle_resume_command(settings_t next,bool restart) {\n'
fragment += source[resume_start:resume_end] + '\n}\n'
evcc_source = (root / 'main/evcc_http.inc').read_text(encoding='utf-8')
ready_start = evcc_source.index('static bool evcc_ready_locked(int64_t now) {')
ready_end = evcc_source.index('static esp_err_t evcc_config_get', ready_start)
release_start = evcc_source.index('static esp_err_t evcc_release_post(httpd_req_t *req) {')
fragment += evcc_source[ready_start:ready_end] + evcc_source[release_start:]
(root / 'tests/phase_control_actual.inc').write_text(fragment, encoding='utf-8')
