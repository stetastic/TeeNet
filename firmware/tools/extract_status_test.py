"""Exercise the actual status serializer with deterministic host-side inputs."""
from pathlib import Path

root = Path(__file__).resolve().parents[1]
source = (root / 'main/app.c').read_text(encoding='utf-8')
runtime = (root / 'main/heatpump_runtime.inc').read_text(encoding='utf-8').split('static bool heatpump_mqtt_locked(')[0]
helpers = source[source.index('static cJSON *number_array('):source.index('static bool read_house_serial(')]
serializer = source[source.index('static const char *vehicle_state_name('):source.index('static void publish_value(')]
telemetry = (root / 'main/telemetry_http.inc').read_text(encoding='utf-8')
telemetry = telemetry[telemetry.index('static void telemetry_json('):telemetry.index('static esp_err_t telemetry_post(')]
assert 'status_json_profile(bool include_token,bool include_diagnostics)' in serializer, 'profile serializer missing'
(root / 'tests/status_json_generated.inc').write_text(
    '/* Generated from main/app.c and telemetry_http.inc; do not edit. */\n#include "json_owned.h"\n' + runtime + helpers + telemetry + serializer,
    encoding='utf-8',
)
