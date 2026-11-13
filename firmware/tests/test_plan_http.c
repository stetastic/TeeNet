#include "ems_core.h"
#include "soc_goal.h"
static bool phase_mqtt_saving;
#include "ems_features.h"
#include "cJSON.h"
#include <assert.h>
#include <math.h>
#include <stdio.h>
#include <string.h>
typedef int esp_err_t;
typedef struct {const char *body;int status;} httpd_req_t;
#define ESP_OK 0
#define HTTPD_400_BAD_REQUEST 400
#define HTTPD_500_INTERNAL_SERVER_ERROR 500
static settings_t settings;
static charge_plan_t charge_plan;
static soc_goal_t soc_goal;
static float direct_measured_soc=NAN;
static float plan_car_soc_locked(int64_t now){(void)now;return direct_measured_soc;}
static struct {bool active;} vehicle_plan;
static plan_phase_t plan_phase;
static pv_control_t pv_control;
static energy_t energy;
static charge_guard_t charge_guard;
static struct {bool configured;} evcc_control;
static bool evcc_configuring,reboot_required,expert_reboot_pending,restarting,ota_in_progress,plan_dirty,write_ok=true;
static bool wallbox_ready=true,meter_ready=true,backup_in_progress,phase_test_active,phase_config_saving,boot_validation_pending,boot_validation_failed,runtime_watchdog_failed;
enum {PHASE_READY,PHASE_STOPPING};static unsigned phase_state=PHASE_READY;
static int64_t wallbox_at=1000,meter_at=1000,actual_at=1000,house_power_at=1000;
static float house_power_w;
static bool feedback_ready(int64_t now){(void)now;return meter_ready;}
static int64_t wallbox_meter_ttl(void){return 5000;}
static int64_t house_meter_ttl(void){return 5000;}
static const char *response_error;
static unsigned plan_revision,restores,saves;
static int locks;
static int64_t utc=1791720000000LL;
#define LOCK() assert(locks++==0)
#define UNLOCK() assert(--locks==0)
static bool restart_pending(void){return reboot_required||expert_reboot_pending;}
static bool authorized(httpd_req_t *r){(void)r;return true;}
static cJSON *read_json(httpd_req_t *r){return cJSON_Parse(r->body);}
static int64_t now_ms(void){return 1000;}
static float active_min_a(void){return settings.min_charge_a;}
static uint32_t calendar(int64_t *epoch,int64_t *midnight){*epoch=utc;*midnight=0;return 20261011;}
static void restore_control_memory_locked(int64_t now,int64_t epoch,bool force){
    assert(locks==1&&now==1000&&epoch==utc&&force&&!settings.enabled);restores++;
}
static void event_locked(const char *event){assert(locks==1&&event);}
static bool save_plan(void){assert(!locks);saves++;return write_ok;}
static int httpd_resp_set_status(httpd_req_t *r,const char *status){r->status=!strcmp(status,"409 Conflict")?409:500;return 0;}
static int httpd_resp_sendstr(httpd_req_t *r,const char *text){(void)r;assert(text);return 0;}
static int httpd_resp_send_err(httpd_req_t *r,int status,const char *text){r->status=status;assert(text);response_error=text;return 0;}
static int ok_json(httpd_req_t *r){r->status=200;return 0;}
static void vehicle_plan_clear_locked(void){vehicle_plan.active=false;}
#include "plan_http.inc"
static void fixture(void){
    settings_defaults(&settings);settings.basic_mode=false;settings.zero_feed_enabled=settings.charge_plan_enabled=settings.control_verified=true;
    memset(&charge_plan,0,sizeof(charge_plan));memset(&pv_control,0,sizeof(pv_control));
    memset(&energy,0,sizeof(energy));memset(&charge_guard,0,sizeof(charge_guard));
    evcc_control.configured=evcc_configuring=reboot_required=expert_reboot_pending=restarting=ota_in_progress=plan_dirty=false;
    plan_phase=PLAN_OFF;plan_revision=restores=saves=0;write_ok=true;
    response_error=NULL;direct_measured_soc=NAN;soc_goal=(soc_goal_t){0};vehicle_plan.active=false;wallbox_ready=meter_ready=true;wallbox_at=meter_at=actual_at=house_power_at=1000;phase_state=PHASE_READY;house_power_w=0;
}
int main(void){
    (void)restart_pending;(void)restore_control_memory_locked;
    const char *start="{\"active\":true,\"kwh\":10,\"deadline_epoch\":1791727200}";
    fixture();expert_reboot_pending=true;httpd_req_t req={start,0};plan_handler(&req);
    assert(req.status==400&&!settings.enabled&&!charge_plan.active&&saves==0&&restores==0);
    fixture();settings.control_verified=false;req=(httpd_req_t){start,0};plan_handler(&req);
    assert(req.status==200&&settings.enabled&&settings.mode==MODE_PV&&charge_plan.active&&saves==1&&restores==1);
    settings.enabled=false;vehicle_plan.active=true;req=(httpd_req_t){"{\"active\":true,\"resume\":true}",0};plan_handler(&req);
    assert(req.status==400&&!settings.enabled&&vehicle_plan.active&&charge_plan.active&&saves==1);
    /* Cancellation remains available while an expert restart is pending. */
    settings.enabled=false;expert_reboot_pending=true;req=(httpd_req_t){"{\"active\":false}",0};plan_handler(&req);
    assert(req.status==200&&!settings.enabled&&!charge_plan.active);
    fixture();write_ok=false;req=(httpd_req_t){start,0};plan_handler(&req);
    assert(req.status==500&&!settings.enabled&&!charge_plan.active);
    fixture();evcc_control.configured=true;req=(httpd_req_t){start,0};plan_handler(&req);
    assert(req.status==409&&saves==0&&restores==0);
    fixture();meter_at=0;req=(httpd_req_t){start,0};plan_handler(&req);assert(req.status==400&&!settings.enabled&&strstr(response_error,"Wallbox-Zaehlers"));
    fixture();house_power_at=0;req=(httpd_req_t){start,0};plan_handler(&req);assert(req.status==400&&!settings.enabled&&strstr(response_error,"Hauszaehlers"));
    fixture();wallbox_at=0;req=(httpd_req_t){start,0};plan_handler(&req);assert(req.status==400&&!settings.enabled&&strstr(response_error,"RS485"));
    fixture();settings.external_mode_input_enabled=true;req=(httpd_req_t){start,0};plan_handler(&req);assert(req.status==400&&!settings.enabled);
    fixture();req=(httpd_req_t){"{\"active\":true,\"kwh\":0,\"deadline_epoch\":1791727200}",0};plan_handler(&req);assert(req.status==400&&strstr(response_error,"Nachlademenge"));
    fixture();req=(httpd_req_t){"{\"active\":true,\"kwh\":10,\"deadline_epoch\":1791719999}",0};plan_handler(&req);assert(req.status==400&&strstr(response_error,"Endzeit"));
    fixture();settings.zero_feed_enabled=false;house_power_at=0;settings.manual_current_a=10;
    req=(httpd_req_t){"{\"active\":true,\"kwh\":10,\"deadline_epoch\":1791727200,\"mode\":\"manual\"}",0};plan_handler(&req);
    assert(req.status==200&&settings.mode==MODE_MANUAL&&charge_plan.grid&&settings.manual_current_a==10&&soc_goal.manual);
    fixture();settings.manual_current_a=10;direct_measured_soc=97;
    req=(httpd_req_t){"{\"active\":true,\"capacity_kwh\":60,\"current_soc\":50,\"target_soc\":100,\"deadline_epoch\":1791727200,\"mode\":\"manual\"}",0};plan_handler(&req);
    assert(req.status==200&&soc_goal.active&&soc_goal.last_soc==97&&fabsf(charge_plan.target_wh-2000)<.01f&&settings.manual_current_a==10);
    settings.enabled=false;settings.manual_current_a=16;settings.manual_phases=1;direct_measured_soc=98;
    req=(httpd_req_t){"{\"active\":true,\"resume\":true,\"mode\":\"pv\"}",0};plan_handler(&req);
    assert(req.status==400&&!settings.enabled&&strstr(response_error,"Betriebsart"));
    req=(httpd_req_t){"{\"active\":true,\"resume\":true}",0};plan_handler(&req);
    assert(req.status==200&&settings.enabled&&settings.mode==MODE_MANUAL&&settings.manual_current_a==10&&settings.manual_phases==3&&soc_goal.last_soc==98);
    assert(fabsf(charge_plan.target_wh-1333.3333f)<.1f);
    settings.enabled=false;direct_measured_soc=100;
    req=(httpd_req_t){"{\"active\":true,\"resume\":true}",0};plan_handler(&req);
    assert(req.status==400&&!settings.enabled&&strstr(response_error,"bereits erreicht"));
    assert(!locks);puts("PASS: actual plan endpoint respects expert restart, restores timers before start, permits cancellation and handles storage failure/evcc ownership");
}
