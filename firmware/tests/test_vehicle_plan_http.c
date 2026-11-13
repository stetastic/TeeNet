#include "ems_core.h"
#include "soc_goal.h"
#include "ems_features.h"
#include "evcc_bridge.h"
#include <assert.h>
#include <errno.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
typedef int esp_err_t;
typedef int nvs_handle_t;
typedef struct {const char *body;int status;} httpd_req_t;
enum {ESP_OK=0,NVS_READONLY=0,HTTPD_400_BAD_REQUEST=400,HTTPD_500_INTERNAL_SERVER_ERROR=500};
static settings_t settings;
static charge_plan_t charge_plan;
static soc_goal_t soc_goal;
static plan_phase_t plan_phase;
static energy_t energy;
static evcc_shell_t evcc_shell;
static struct {bool configured;} evcc_control;
static struct {bool stable;} evu_contact;
static pv_control_t pv_control;
static charge_guard_t charge_guard;
static int64_t evcc_shell_at=99999,car_soc_at,test_now=100000;
static float car_soc;
static bool config_storage_ok=true,mqtt_online=true,plan_dirty,evcc_configuring,restarting,ota_in_progress,backup_in_progress,boot_validation_pending,write_ok=true;
static bool wallbox_ready=true,meter_ready=true,phase_mqtt_saving,phase_test_active,phase_config_saving,boot_validation_failed,runtime_watchdog_failed;
enum {PHASE_READY,PHASE_STOPPING};static unsigned phase_state=PHASE_READY;
static int64_t wallbox_at=100000,meter_at=100000,actual_at=100000,house_power_at=100000;
static float house_power_w;
static bool feedback_ready(int64_t now){(void)now;return meter_ready;}
static int64_t wallbox_meter_ttl(void){return 5000;}
static int64_t house_meter_ttl(void){return 5000;}
static unsigned plan_revision;
static int locks;
#define LOCK() assert(locks++==0)
#define UNLOCK() assert(--locks==0)
static int64_t now_ms(void){return test_now;}
static int nvs_open(const char *a,int b,nvs_handle_t *c){(void)a;(void)b;(void)c;return 1;}
static int nvs_get_blob(int a,const char *b,void *c,size_t *d){(void)a;(void)b;(void)c;(void)d;return 1;}
static void nvs_close(int a){(void)a;}
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wunused-function"
#include "vehicle_runtime.inc"
#pragma GCC diagnostic pop
static bool authorized(httpd_req_t *r){(void)r;return true;}
static cJSON *read_json(httpd_req_t *r){return cJSON_Parse(r->body);}
static bool restart_pending(void){return false;}
static bool house_currents_ready_locked(int64_t now){(void)now;return true;}
static uint32_t calendar(int64_t *epoch,int64_t *midnight){*epoch=1793300000000LL;*midnight=0;return 20261029;}
static void restore_control_memory_locked(int64_t now,int64_t epoch,bool force){assert(locks==1&&now==test_now&&epoch>0&&force);}
static float active_min_a(void){return settings.min_charge_a;}
static void event_locked(const char *event){assert(event&&locks==1);}
static bool save_plan(void){assert(!locks);return write_ok;}
static int httpd_resp_send_err(httpd_req_t *r,int status,const char *message){assert(message);r->status=status;return ESP_OK;}
static cJSON *vehicle_snapshot(void){return cJSON_CreateObject();}
static int send_json(httpd_req_t *r,cJSON *o){cJSON_Delete(o);r->status=200;return 0;}
#include "vehicle_plan_http_actual.inc"
#include "vehicle_estimate_http.inc"
static void fixture(void){
 soc_goal=(soc_goal_t){0};settings_defaults(&settings);settings.basic_mode=false;settings.control_verified=settings.zero_feed_enabled=settings.charge_plan_enabled=true;settings.phase_switch_enabled=true;settings.enabled=false;
 vehicles=(vehicle_store_t){.version=1,.enabled=true};vehicles.rows[0]=(vehicle_profile_t){.name="A",.cards={"CARD-A",""},.capacity_kwh=60,.efficiency=.9,.maximum_kw=11,.target_soc=80};
 charge_plan=(charge_plan_t){0};vehicle_plan_clear_locked();memset(vehicle_soc_at,0,sizeof(vehicle_soc_at));car_soc_at=0;
 evcc_shell=(evcc_shell_t){.state='B',.session={.present=true,.card="CARD-A",.identity="T1"}};evcc_shell_at=99999;evcc_control.configured=false;write_ok=true;
}
int main(void){
 fixture();httpd_req_t estimation={.body="{\"slot\":1,\"start_soc\":30}"};vehicle_estimate_post(&estimation);assert(estimation.status==200&&vehicle_estimate.active&&!settings.enabled);
 estimation=(httpd_req_t){.body="{\"slot\":1,\"start_soc\":101}"};vehicle_estimate_post(&estimation);assert(estimation.status==HTTPD_400_BAD_REQUEST&&!settings.enabled);
 estimation=(httpd_req_t){.body="{\"slot\":2,\"start_soc\":30}"};vehicle_estimate_post(&estimation);assert(estimation.status==HTTPD_400_BAD_REQUEST);

 const char *start="{\"slot\":1,\"current_soc\":20,\"target_soc\":80,\"deadline_epoch\":1793314400}";
 fixture();settings.control_verified=false;httpd_req_t r={start,0};vehicle_plan_post(&r);assert(r.status==200&&settings.enabled&&charge_plan.active&&fabsf(charge_plan.target_wh-40000)<.01);
 assert(vehicle_plan.slot==0&&!strcmp(vehicle_plan.card,"CARD-A")&&!vehicle_plan.identity[0]);
 fixture();settings.vehicle_fixed_profile=1;evcc_shell.session=(vehicle_session_t){0};vehicles.rows[0].cards[0][0]=0;
 r=(httpd_req_t){start,0};vehicle_plan_post(&r);assert(r.status==200&&settings.enabled&&charge_plan.active&&!vehicle_plan.card[0]);
 evcc_shell.state='C';strcpy(evcc_shell.session.identity,"FIRST-CHARGE");vehicle_plan_step_locked(test_now);assert(settings.enabled&&!strcmp(vehicle_plan.identity,"FIRST-CHARGE"));
 fixture();settings.vehicle_fixed_profile=1;evcc_shell_at=0;evcc_shell=(evcc_shell_t){0};estimation=(httpd_req_t){.body="{\"slot\":1,\"start_soc\":30}"};vehicle_estimate_post(&estimation);assert(estimation.status==200);
 r=(httpd_req_t){start,0};vehicle_plan_post(&r);assert(r.status==400&&!settings.enabled); /* Planner still requires fresh Wallbox readiness. */
 fixture();evcc_shell.session.card[0]=0;r=(httpd_req_t){start,0};vehicle_plan_post(&r);assert(r.status==400&&!settings.enabled&&!charge_plan.active);
 fixture();strcpy(evcc_shell.session.card,"OTHER");r=(httpd_req_t){start,0};vehicle_plan_post(&r);assert(r.status==400&&!settings.enabled);
 for(const char *state="ADF?";*state;state++){fixture();evcc_shell.state=*state;r=(httpd_req_t){start,0};vehicle_plan_post(&r);assert(r.status==400&&!settings.enabled&&!charge_plan.active);}
 fixture();r=(httpd_req_t){"{\"slot\":-2147483648,\"current_soc\":20,\"target_soc\":80,\"deadline_epoch\":1793314400}",0};vehicle_plan_post(&r);assert(r.status==400&&!settings.enabled);
 fixture();r=(httpd_req_t){"{\"slot\":1,\"current_soc\":20,\"target_soc\":80,\"deadline_epoch\":1793314400,\"manual_assignment\":true}",0};vehicle_plan_post(&r);assert(r.status==400&&!charge_plan.active);
 fixture();settings.phase_switch_enabled=false;settings.fixed_charge_phases=3;vehicles.rows[0].maximum_kw=3.7f;r=(httpd_req_t){start,0};vehicle_plan_post(&r);assert(r.status==400&&!charge_plan.active&&!vehicle_plan.active);
 fixture();evcc_shell_at=1;r=(httpd_req_t){start,0};vehicle_plan_post(&r);assert(r.status==400&&!settings.enabled);
 fixture();evcc_control.configured=true;r=(httpd_req_t){start,0};vehicle_plan_post(&r);assert(r.status==400&&!settings.enabled);
 fixture();vehicles.rows[0].soc_source=2;settings.vehicle_soc_enabled=true;car_soc=31;car_soc_at=test_now;r=(httpd_req_t){start,0};vehicle_plan_post(&r);assert(r.status==200&&fabsf(charge_plan.target_wh-32666.667f)<.1f&&vehicle_plan.start_soc==31);
 fixture();write_ok=false;r=(httpd_req_t){start,0};vehicle_plan_post(&r);assert(r.status==500&&!settings.enabled&&!charge_plan.active);
 fixture();settings.manual_current_a=10;settings.zero_feed_enabled=false;
 r=(httpd_req_t){"{\"slot\":1,\"current_soc\":20,\"target_soc\":80,\"deadline_epoch\":1793314400,\"mode\":\"manual\"}",0};vehicle_plan_post(&r);assert(r.status==200&&charge_plan.grid&&settings.mode==MODE_MANUAL&&soc_goal.manual&&settings.manual_current_a<=10.001f);
 assert(vehicle_revision==1&&!locks);puts("PASS: actual vehicle-plan HTTP endpoint: fixed profile without RFID or matching card, fresh Wallbox readiness, first-charge binding, measured SOC, power limits and storage fail-stop");
}
