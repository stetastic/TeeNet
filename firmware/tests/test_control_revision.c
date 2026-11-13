#include "ems_core.h"
#include "shell_http_runtime.h"
static shell_http_runtime_t http_regulator;
static manual_report_filter_t manual_filter;
#include "cJSON.h"
#include <assert.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define CONFIG_TEE_HTTP_CONTROL_TRIAL 1
#define pdTRUE 1
enum {PHASE_READY,PHASE_STOPPING,PHASE_SETTLING,PHASE_FAULT,PLAN_OFF};
static settings_t settings;
static bool terms_accepted=true,evcc_configuring,phase_mqtt_saving;
static bool battery_use,battery_start_use,restarting,ota_in_progress,phase_test_active,phase_config_saving;
static bool phase_shelly_uncertain,phase_shelly_pending,phase_mqtt_pending,phase_mqtt_known,phase_idle_inhibit;
static bool plan_dirty;
static int phase_state=PHASE_READY,phase_goal,plan_phase,maintenance_flash_lock;
static uint32_t plan_revision;
static int64_t phase_stop_at,phase_zero_at,phase_move_at,phase_hold_until,house_power_at,actual_at,pv_at;
static float house_power_w;
static struct {bool configured,enabled;} evcc_control;
static struct {bool latched;} charge_guard;
static struct {bool active;} charge_plan;
static struct {bool started,active;int remaining_ms;} battery_buffer;
static struct {int value;} wallbox_meter_hold;
static struct {uint32_t sequence;} event_log;
static unsigned lock_depth,restores,transitions,stop_events,change_events;
static bool inject_operator_stop;
static void lock_state(void){
    assert(!lock_depth++);
    /* Model an operator command after observation but before mutation lock. */
    if(inject_operator_stop){event_log.sequence++;settings.enabled=false;inject_operator_stop=false;}
}
#define LOCK() lock_state()
#define UNLOCK() assert(lock_depth--==1)
static uint32_t calendar(int64_t *epoch,int64_t *midnight){*epoch=*midnight=0;return 0;}
static int64_t now_ms(void){return 1000;}
static float target_locked(int64_t now){(void)now;assert(lock_depth);return settings.enabled?settings.manual_current_a:0;}
static bool evcc_emergency_stop_json(const char *s){(void)s;return true;}
static void evcc_local_stop(void *p){(void)p;evcc_control.enabled=false;}
static bool phase_can_unlock_locked(int64_t now){(void)now;return true;}
static bool restart_pending(void){return false;}
static void restore_control_memory_locked(int64_t a,int64_t b,bool c){(void)a;(void)b;(void)c;assert(lock_depth);restores++;}
static void phase_test_end_locked(void){phase_test_active=false;}
static bool phase_shelly_remote(void){return false;}
static bool phase_mqtt_remote(void){return false;}
static unsigned phase_observed(void){return 3;}
static int64_t house_meter_ttl(void){return 5000;}
static int64_t wallbox_meter_ttl(void){return 5000;}
static float pv_available_current_locked(int64_t now,float watts){(void)now;(void)watts;return 0;}
static void mode_transition_locked(control_mode_t mode,float target,int64_t now){(void)mode;(void)target;(void)now;assert(lock_depth);transitions++;}
static void event_locked(const char *reason){
    assert(lock_depth);event_log.sequence++;
    if(!strcmp(reason,"user_stop"))stop_events++;
    else if(!strcmp(reason,"control_changed"))change_events++;
}
static int ticks(int x){return x;}
static int xSemaphoreTake(int semaphore,int timeout){(void)semaphore;(void)timeout;return pdTRUE;}
static void xSemaphoreGive(int semaphore){(void)semaphore;}
static bool save_control_memory(bool force){(void)force;return true;}
#include "control_revision_actual.inc"

static bool control(const char *text){
    cJSON *o=cJSON_Parse(text);assert(o);bool ok=apply_control(o);cJSON_Delete(o);assert(!lock_depth);return ok;
}
static void fixture(void){
    settings_defaults(&settings);settings.mode=MODE_MANUAL;settings.enabled=false;
    settings.manual_current_a=10;settings.fixed_charge_phases=settings.manual_phases=settings.charge_phases=3;
    event_log.sequence=42;restores=transitions=stop_events=change_events=0;
    inject_operator_stop=false;wallbox_meter_hold.value=123;
}
static void unchanged(const settings_t *before,unsigned revision){
    assert(!memcmp(before,&settings,sizeof(settings))&&event_log.sequence==revision);
    assert(!restores&&!transitions&&!stop_events&&!change_events&&wallbox_meter_hold.value==123);
}
int main(void){
    fixture();assert(control("{\"enabled\":true,\"current_a\":12,\"expected_event_sequence\":42}"));
    assert(settings.enabled&&settings.manual_current_a==12&&event_log.sequence==43&&change_events==1);
    fixture();settings_t before=settings;
    assert(!control("{\"enabled\":true,\"current_a\":12,\"expected_event_sequence\":41}"));unchanged(&before,42);
    /* A rejected guarded Stop also has no partial side effects; the fallback
       is a plain Stop with no expectation and must remain available. */
    fixture();settings.enabled=true;before=settings;
    assert(!control("{\"enabled\":false,\"expected_event_sequence\":41}"));unchanged(&before,42);
    assert(control("{\"enabled\":false}"));assert(!settings.enabled&&event_log.sequence==43&&stop_events==1);
    fixture();settings.enabled=true;inject_operator_stop=true;
    assert(!control("{\"enabled\":true,\"current_a\":12,\"expected_event_sequence\":42}"));
    assert(!settings.enabled&&settings.manual_current_a==10&&event_log.sequence==43&&!restores&&!change_events);
    const char *invalid[]={
        "{\"enabled\":true,\"expected_event_sequence\":null}",
        "{\"enabled\":true,\"expected_event_sequence\":true}",
        "{\"enabled\":true,\"expected_event_sequence\":\"42\"}",
        "{\"enabled\":true,\"expected_event_sequence\":-1}",
        "{\"enabled\":true,\"expected_event_sequence\":42.5}",
        "{\"enabled\":true,\"expected_event_sequence\":4294967296}",
        "{\"enabled\":true,\"expected_event_sequence\":1e999}",
        "{\"enabled\":true,\"expected_event_sequence\":42,\"expected_event_sequence\":43}"
    };
    for(unsigned i=0;i<sizeof(invalid)/sizeof(invalid[0]);i++){
        fixture();before=settings;assert(!control(invalid[i]));unchanged(&before,42);
    }
    fixture();event_log.sequence=UINT32_MAX;
    assert(control("{\"enabled\":false,\"expected_event_sequence\":4294967295}"));
    assert(!settings.enabled&&event_log.sequence==0&&stop_events==1);
    puts("PASS: actual control mutation checks revision atomically; stale/invalid requests preserve state; unconditional Stop remains available");
}
