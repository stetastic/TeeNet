#include "ems_core.h"
#include "ems_features.h"
#include "evcc_bridge.h"
#include <assert.h>
#include <errno.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
typedef int nvs_handle_t;
typedef int esp_err_t;
enum {ESP_OK=0,NVS_READONLY=0};
static int nvs_open(const char *a,int b,nvs_handle_t *c){(void)a;(void)b;(void)c;return 1;}
static int nvs_get_blob(int a,const char *b,void *c,size_t *d){(void)a;(void)b;(void)c;(void)d;return 1;}
static void nvs_close(int a){(void)a;}
static settings_t settings;
static bool config_storage_ok,mqtt_online=true,plan_dirty;
static unsigned plan_revision;
static charge_plan_t charge_plan;
static plan_phase_t plan_phase;
static energy_t energy;
static evcc_shell_t evcc_shell;
static int64_t evcc_shell_at,car_soc_at,test_now=100000;
static float car_soc;
static int64_t now_ms(void){return test_now;}
#include "vehicle_runtime.inc"
static void fixture(void){
 settings_defaults(&settings);settings.enabled=true;settings.mode=MODE_PV;settings.phase_switch_enabled=true;
 vehicles=(vehicle_store_t){.version=1,.enabled=true};
 vehicles.rows[0]=(vehicle_profile_t){.name="A",.cards={"CARD-A",""},.capacity_kwh=60,.efficiency=.9,.target_soc=80,.maximum_kw=11,.soc_source=1};
 vehicle_plan=(vehicle_plan_t){.version=1,.active=true,.slot=0,.start_soc=20,.last_soc=20,.target_soc=80,.capacity_kwh=60,.efficiency=.9,.maximum_kw=11,.card="CARD-A",.identity="T1"};
 charge_plan=(charge_plan_t){.version=1,.active=true,.target_wh=40000,.delivered_wh=10000,.deadline_s=1793400000,.last_total_wh=100000};
 energy.store.total_wh[0]=110000;test_now=100000;evcc_shell_at=99999;evcc_shell=(evcc_shell_t){.state='C',.session={.present=true,.card="CARD-A",.identity="T1"}};
 memset(vehicle_soc_at,0,sizeof(vehicle_soc_at));vehicle_missing_since=0;vehicle_plan_reason[0]=0;mqtt_online=true;plan_dirty=false;
}
int main(void){
 vehicle_load();assert(vehicle_revision==1);
 fixture();memset(&vehicle_estimate,0,sizeof(vehicle_estimate));assert(vehicle_estimate_begin_locked(0,20,test_now));
 energy.store.total_wh[0]+=10000;float guessed=0;assert(vehicle_estimate_soc_locked(0,test_now,&guessed)&&fabsf(guessed-35)<.01f);
 assert(vehicle_soc_message_locked("input/vehicle_1_soc_pct","40"));vehicle_estimate_step_locked(test_now);
 assert(vehicle_soc_message_locked("input/vehicle_1_soc_pct","unavailable"));energy.store.total_wh[0]+=1000;
 assert(vehicle_estimate_soc_locked(0,test_now,&guessed)&&fabsf(guessed-41.5f)<.01f);
 strcpy(evcc_shell.session.identity,"Other car");assert(!vehicle_estimate_soc_locked(0,test_now,&guessed));vehicle_estimate_step_locked(test_now);assert(!vehicle_estimate.active);
 assert(!vehicle_estimate_begin_locked(0,-1,test_now));
 assert(!vehicle_estimated_soc(20,60,.9,1000,999,&guessed));
 assert(vehicle_estimated_soc(99,60,.9,1000,999999,&guessed)&&guessed==100);
fixture();assert(vehicle_current_locked(test_now)==0);
 /* A fixed vehicle needs neither RFID nor HTTP for display/manual estimation. */
 settings.vehicle_fixed_profile=1;evcc_shell_at=0;evcc_shell=(evcc_shell_t){0};vehicles.rows[0].cards[0][0]=0;
 assert(vehicle_current_locked(test_now)==0);assert(vehicle_estimate_begin_locked(0,30,test_now));
 energy.store.total_wh[0]+=1000;assert(vehicle_estimate_soc_locked(0,test_now,&guessed)&&fabsf(guessed-31.5f)<.01f);
 settings.vehicle_fixed_profile=0;vehicle_estimate_step_locked(test_now);assert(!vehicle_estimate.active);
 fixture();settings.vehicle_fixed_profile=4;assert(vehicle_current_locked(test_now)==-1);
 fixture();evcc_shell.state='B';strcpy(evcc_shell.session.identity,"previous transaction");assert(vehicle_estimate_begin_locked(0,30,test_now));
 evcc_shell.state='C';strcpy(evcc_shell.session.identity,"new transaction");vehicle_estimate_step_locked(test_now);assert(vehicle_estimate.active&&!strcmp(vehicle_estimate.identity,"new transaction"));
 strcpy(evcc_shell.session.identity,"another transaction");vehicle_estimate_step_locked(test_now);assert(!vehicle_estimate.active);
 fixture();settings.vehicle_fixed_profile=1;evcc_shell.session.card[0]=0;evcc_shell.session.present=false;vehicle_plan_step_locked(test_now);assert(settings.enabled&&charge_plan.active);
 settings.vehicle_fixed_profile=2;vehicle_plan_step_locked(test_now);assert(!charge_plan.active&&!settings.enabled);
 fixture();settings.vehicle_fixed_profile=1;evcc_shell.state='A';vehicle_plan_step_locked(test_now);assert(!settings.enabled);test_now+=15001;evcc_shell_at=test_now;vehicle_plan_step_locked(test_now);assert(!charge_plan.active);
 fixture();assert(vehicle_current_locked(test_now)==0);
 assert(vehicle_soc_message_locked("input/vehicle_1_soc_pct","50"));assert(vehicle_soc_fresh_locked(0,test_now));
 vehicle_plan_step_locked(test_now);assert(fabsf(charge_plan.target_wh-30000)<.01&&charge_plan.last_total_wh==110000&&vehicle_plan.anchor_wh==10000);
 assert(vehicle_soc_message_locked("input/vehicle_1_soc_pct","50"));vehicle_plan_step_locked(test_now);assert(fabsf(charge_plan.target_wh-30000)<.01);
 assert(vehicle_soc_message_locked("input/vehicle_1_soc_pct","unavailable"));assert(!vehicle_soc_fresh_locked(0,test_now));
 assert(vehicle_soc_message_locked("input/vehicle_1_soc_pct","80"));vehicle_plan_step_locked(test_now);assert(charge_plan.delivered_wh==charge_plan.target_wh);
 assert(charge_plan_step(&charge_plan,1793300000,true,true,energy.store.total_wh[0],11)==PLAN_COMPLETE);
 fixture();strcpy(evcc_shell.session.card,"OTHER");vehicle_plan_step_locked(test_now);assert(!settings.enabled&&!charge_plan.active&&!strcmp(vehicle_plan_reason,"vehicle_changed"));
 fixture();strcpy(evcc_shell.session.identity,"T2");vehicle_plan_step_locked(test_now);assert(!settings.enabled&&!charge_plan.active);
 fixture();evcc_shell.session.card[0]=0;vehicle_plan_step_locked(test_now);assert(!settings.enabled&&charge_plan.active&&!strcmp(vehicle_plan_reason,"vehicle_card_missing"));
 strcpy(evcc_shell.session.card,"CARD-A");vehicle_plan_step_locked(test_now);assert(!settings.enabled&&charge_plan.active); /* Card recovery never resumes a paused plan. */
 strcpy(evcc_shell.session.identity,"T2");evcc_shell.session.card[0]=0;vehicle_plan_step_locked(test_now);assert(!charge_plan.active&&!strcmp(vehicle_plan_reason,"vehicle_changed"));
 fixture();evcc_shell.state='A';vehicle_plan_step_locked(test_now);assert(!settings.enabled&&charge_plan.active);test_now+=15001;evcc_shell_at=test_now;vehicle_plan_step_locked(test_now);assert(!charge_plan.active);
 fixture();evcc_shell.state='F';vehicle_plan_step_locked(test_now);assert(!settings.enabled&&charge_plan.active&&!strcmp(vehicle_plan_reason,"vehicle_not_ready"));
 fixture();evcc_shell_at=1;vehicle_plan_step_locked(test_now);assert(!settings.enabled&&charge_plan.active&&!strcmp(vehicle_plan_reason,"wallbox_unavailable"));
 evcc_shell_at=test_now;vehicle_plan_step_locked(test_now);assert(!settings.enabled); /* Fresh HTTP never resumes by itself. */
 fixture();evcc_shell.session=(vehicle_session_t){0};evcc_shell.state='A';vehicle_plan_step_locked(test_now);test_now+=15001;evcc_shell_at=test_now;vehicle_plan_step_locked(test_now);assert(!settings.enabled&&!charge_plan.active);
 fixture();vehicle_plan.maximum_kw=3.7f;assert(fabsf(vehicle_phase_surplus_locked(9000)-3700)<.01);assert(vehicle_limit_current_locked(16)==0);
 vehicle_grid_current_locked(vehicle_plan_maximum_locked(11));assert(settings.manual_phases==1&&settings.manual_current_a<=16.001);
 settings.charge_phases=1;assert(vehicle_limit_current_locked(16)>15.9);
 settings.phase_switch_enabled=false;settings.fixed_charge_phases=3;assert(vehicle_plan_maximum_locked(11)==0);
 fixture();vehicles.rows[0].soc_source=2;settings.vehicle_soc_enabled=true;car_soc=31;car_soc_at=test_now;assert(vehicle_soc_fresh_locked(0,test_now)&&vehicle_soc_value_locked(0)==31);
 mqtt_online=false;assert(!vehicle_soc_fresh_locked(0,test_now));vehicle_plan_clear_locked();assert(!vehicle_plan.active);
 puts("PASS: actual profile runtime, per-car SOC, stale/invalid data, SOC anchoring, target completion, identity/unplug cancellation, no restart on recovery and one-phase power caps");
}
