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
publisher = source[source.index('static void mqtt_state(void)'):source.index('static bool apply_control(')]
snapshot_choice = 'full_json?status_json_profile(false,true):mqtt_status_snapshot()'
assert snapshot_choice in publisher, 'MQTT snapshot routing missing'
legacy_publisher = publisher.replace('static void mqtt_state(void)', 'static void mqtt_state_full_fixture(void)', 1).replace(snapshot_choice, 'status_json_profile(false,full_json)', 1)
publish_value = source[source.index('static void publish_value('):source.index('static cJSON *homeassistant_entity(')]
(root / 'tests/mqtt_publish_generated.inc').write_text(
    '/* Generated actual publisher plus old snapshot selection as comparison; do not edit. */\n' +
    publish_value + publisher + legacy_publisher, encoding='utf-8')

# Exercise the real acquisition/reacquisition statements with a producer
# publishing a newer sample at LOCK. Keep the controller itself unmodified.
loop = source[source.index('static void status_task(void *arg)'):]
clock_start = loop.index('        int64_t ', loop.index('    while(true)'))
clock_end = loop.index('        personal_sync_locked(now);', clock_start)
reset_start = loop.index('        if(reset_count_pending ', clock_end)
reset_end = loop.index('\n', reset_start)
(root / 'tests/status_clock_actual.inc').write_text(
    '/* Generated status_task clock statements; do not edit. */\n'
    'static int64_t status_decision_clock_fixture(void){\n' + loop[clock_start:clock_end] +
    '    (void)epoch;(void)midnight;(void)date;return now;\n}\n'
    'static int64_t status_reset_clock_fixture(int64_t now){\n' + loop[reset_start:reset_end] +
    '\n    return now;\n}\n', encoding='utf-8')
