#include "ems_core.h"
#include "shelly_phase.h"
#include "cJSON.h"
#include <assert.h>
#include <math.h>
#include <stdio.h>
#include <string.h>
#define CONFIG_TEE_HTTP_CONTROL_TRIAL 1
static struct {bool armed;} normal_offset_trial,shell_http_trial;
typedef int esp_err_t;
#define ESP_OK 0
#define HTTPD_400_BAD_REQUEST 400
typedef struct {int status;const char *body;} httpd_req_t;
enum {PHASE_READY,PHASE_STOPPING,PHASE_MOVING,PHASE_SETTLING,PHASE_FAULT};
static settings_t settings;static bool phase_config_saving,reboot_required;
static int phase_state;static uint8_t phase_goal,phase_candidate;
static bool phase_gpio_ready,relay_ready,phase_idle_inhibit,phase_startup,phase_test_active;
static uint8_t phase_test_goal;static int64_t phase_test_until;
static bool restarting,ota_in_progress,backup_in_progress,evcc_configuring,boot_validation_pending;
static bool wifi_online=true,mqtt_online=true,phase_mqtt_allowed,phase_mqtt_saving,pending;
static bool relay_on[1];static float actual_a[3];
static int64_t actual_at,meter_at,wallbox_at,last_sent_current_at,clock_ms;
static int64_t phase_candidate_at,phase_stop_at,phase_zero_at,phase_move_at,phase_hold_until,phase_attempt_at,phase_no_load_since;
static struct {bool enabled,configured,preparing,inhibited;} evcc_control;
static pv_control_t pv_control;static manual_report_filter_t manual_filter;
static int64_t wallbox_meter_ttl(void){return 5000;}
static int64_t now_ms(void){return clock_ms;}
static bool restart_pending(void){return pending||boot_validation_pending;}
#include "phase_voltage_runtime.inc"
#include "phase_mqtt_runtime.inc"
#include "phase_shelly_runtime.inc"
#include "phase_config_guard.inc"
#include "phase_test_runtime.inc"
static unsigned observed=3,writes;
static uint8_t phase_observed(void){return phase_shelly_remote()?phase_shelly_observed_locked(clock_ms):observed;}
static unsigned evcc_desired_phases(const void *p,unsigned active){(void)p;return active;}
static float phase_surplus_w_locked(int64_t now){(void)now;return 0;}
static bool phase_actuator_begin_locked(uint8_t goal,int64_t now){
    if(phase_mqtt_remote())return phase_mqtt_begin_locked(goal,now);
    if(phase_shelly_remote())return phase_shelly_begin_locked(goal,now);
    assert(phase_test_zero_locked(now)&&actual_at-phase_zero_at>=ems_param_ms(EP_PHASE_ZERO));
    writes++;relay_on[0]=goal==1;observed=goal;return true;
}
static float vehicle_phase_surplus_locked(float watts){return watts;}
#include "phase_controller.inc"
static int locks;static bool auth=true;static int64_t expert_wrong_until;
static struct {uint32_t sequence;} event_log;
static bool inject_operator_event;
static unsigned inject_phase_sample;
static void test_lock(void){
 assert(locks++==0);
 if(inject_operator_event){event_log.sequence++;inject_operator_event=false;}
 if(inject_phase_sample){
  /* A producer finishes its publication before the waiting reader acquires. */
  clock_ms++;
  if(inject_phase_sample==1)phase_shelly_at=clock_ms;
  else{float voltage[3]={235,236,237};phase_voltage_sample(&phase_voltage,voltage,true,clock_ms);}
  inject_phase_sample=0;
 }
}
#define LOCK() test_lock()
#define UNLOCK() assert(--locks==0)
static bool reset_count_pending;
static int64_t reset_clear_at;
static uint32_t calendar(int64_t *epoch,int64_t *midnight){*epoch=1793962800000LL;*midnight=1793919600000LL;return 20261106;}
static void reset_sequence_clear(void){assert(!locks);reset_count_pending=false;inject_phase_sample=2;}
#include "status_clock_actual.inc"
static bool authorized(httpd_req_t *r){if(!auth)r->status=401;return auth;}
static cJSON *read_json(httpd_req_t *r){return cJSON_Parse(r->body);}
static esp_err_t httpd_resp_send_err(httpd_req_t *r,int c,const char *s){(void)s;r->status=c;return ESP_OK;}
static void httpd_resp_set_status(httpd_req_t *r,const char *s){r->status=!strncmp(s,"409",3)?409:429;}
static esp_err_t httpd_resp_sendstr(httpd_req_t *r,const char *s){(void)r;(void)s;return ESP_OK;}
static esp_err_t ok_json(httpd_req_t *r){r->status=200;return ESP_OK;}
static void evcc_local_stop(void *p){(void)p;evcc_control.enabled=evcc_control.preparing=false;evcc_control.inhibited=true;}
static void event_locked(const char *s){assert(locks==1&&!strcmp(s,"phase_hardware_test"));event_log.sequence++;}
#include "phase_test_http.inc"
static int request(const char *s){httpd_req_t r={0,s};phase_test_handler(&r);assert(!locks);return r.status;}
static const char *one="{\"phases\":1,\"pin\":\"4040\",\"confirm_risk\":true}";
static void fixture(void){
 settings_defaults(&settings);settings.basic_mode=false;settings.phase_switch_enabled=settings.relay_board_enabled=settings.phase_feedback_enabled=true;settings.relay1_mode=3;settings.xemex_coils=3;
 settings.enabled=false;settings.mode=MODE_OFF;settings.charge_phases=3;
 phase_gpio_ready=relay_ready=true;phase_state=PHASE_READY;observed=3;writes=0;
 clock_ms=actual_at=meter_at=wallbox_at=last_sent_current_at=1000;
 memset(actual_a,0,sizeof(actual_a));memset(&evcc_control,0,sizeof(evcc_control));
 phase_test_active=phase_idle_inhibit=phase_startup=restarting=ota_in_progress=backup_in_progress=evcc_configuring=boot_validation_pending=pending=false;
 phase_shelly_pending=phase_shelly_inflight=phase_shelly_uncertain=phase_mqtt_pending=phase_mqtt_inflight=false;
 phase_shelly_rechecking=false;phase_shelly_recheck_at=0;
 phase_mqtt_allowed=false;phase_test_until=phase_zero_at=phase_stop_at=expert_wrong_until=0;relay_on[0]=false;
 phase_config_saving=reboot_required=false;auth=wifi_online=mqtt_online=true;
 normal_offset_trial.armed=shell_http_trial.armed=false;
 event_log.sequence=42;inject_operator_event=false;inject_phase_sample=0;
 reset_count_pending=false;reset_clear_at=0;
}
static void step(int64_t now,float amps){clock_ms=actual_at=meter_at=wallbox_at=last_sent_current_at=now;actual_a[0]=actual_a[1]=actual_a[2]=amps;phase_step_locked(now);}
static void clock_fixture(void){
 fixture();settings.phase_actuator_type=1;settings.phase_feedback_source=1;
 settings.phase_shelly_true_three_phase=false;
 phase_shelly_config_ok=true;phase_shelly_at=clock_ms;
 phase_shelly_feedback_value=(shelly_phase_feedback_t){.output=false};
 phase_shelly_transport_misses=phase_shelly_invalid_replies=0;
 memset(&phase_voltage,0,sizeof(phase_voltage));float voltage[3]={235,236,237};
 phase_voltage_sample(&phase_voltage,voltage,true,clock_ms-500);
 phase_voltage_sample(&phase_voltage,voltage,true,clock_ms);
 assert(phase_shelly_observed_locked(clock_ms)==3);
}
static void decision_clock_interleave(void){
 for(unsigned producer=1;producer<=2;producer++){
  /* Reproduce the former ordering: a successful newer publication alone
     caused pruefen, without any transport miss or invalid reply. */
  clock_fixture();int64_t stale=now_ms();inject_phase_sample=producer;LOCK();
  phase_step_locked(stale);UNLOCK();
  assert(phase_state==PHASE_SETTLING&&phase_shelly_rechecking&&!phase_shelly_at);
  assert(!phase_shelly_transport_misses&&!phase_shelly_invalid_replies&&!writes);
  /* Use the actual status_task statements extracted from app.c. */
  clock_fixture();inject_phase_sample=producer;
  int64_t decision=status_decision_clock_fixture();assert(locks==1&&decision==clock_ms);
  phase_step_locked(decision);UNLOCK();
  assert(phase_state==PHASE_READY&&!phase_shelly_rechecking&&phase_shelly_at>0);
  assert(!phase_shelly_transport_misses&&!phase_shelly_invalid_replies&&!writes);
 }
 clock_fixture();LOCK();int64_t before=now_ms();reset_count_pending=true;
 int64_t decision=status_reset_clock_fixture(before);
 assert(locks==1&&decision==clock_ms&&decision>before);
 phase_step_locked(decision);UNLOCK();
 assert(phase_state==PHASE_READY&&!phase_shelly_rechecking&&!writes);
 /* The fix must not admit genuinely future or expired evidence. */
 assert(!fresh(clock_ms-1,clock_ms,5000)&&!fresh(clock_ms+5001,clock_ms,5000));
 assert(!phase_voltage_observed(&phase_voltage,phase_voltage.at-1));
 puts("PASS: newer phase/voltage publication at LOCK reproduces old false recheck; actual post-lock clocks and reset reacquisition retain READY without relaxing freshness");
}
static void phase_revision(void){
 const char *matched="{\"phases\":1,\"pin\":\"4040\",\"confirm_risk\":true,\"expected_event_sequence\":42}";
 fixture();assert(request(matched)==200&&phase_test_active&&phase_state==PHASE_STOPPING&&event_log.sequence==43&&!writes);
 /* Plain cancellation must not depend on the revision used for starting. */
 event_log.sequence=90;assert(request("{\"stop\":true}")==200&&!phase_test_active&&!settings.enabled&&event_log.sequence==90&&!writes);
 fixture();event_log.sequence=43;settings_t before=settings;
 assert(request(matched)==409&&!phase_test_active&&phase_state==PHASE_READY&&!writes);
 assert(!memcmp(&before,&settings,sizeof(settings))&&event_log.sequence==43&&!evcc_control.inhibited);
 fixture();inject_operator_event=true;
 assert(request(matched)==409&&!phase_test_active&&phase_state==PHASE_READY&&!writes&&event_log.sequence==43);
 const char *invalid[]={"null","true","\"42\"","-1","42.5","4294967296","1e999","42,\"expected_event_sequence\":43"};
 for(unsigned i=0;i<sizeof(invalid)/sizeof(invalid[0]);i++){
  fixture();before=settings;char json[180];
  snprintf(json,sizeof(json),"{\"phases\":1,\"pin\":\"4040\",\"confirm_risk\":true,\"expected_event_sequence\":%s}",invalid[i]);
  assert(request(json)==400&&!phase_test_active&&phase_state==PHASE_READY&&!writes&&event_log.sequence==42);
  assert(!memcmp(&before,&settings,sizeof(settings))&&!evcc_control.inhibited);
 }
 fixture();event_log.sequence=UINT32_MAX;
 assert(request("{\"phases\":1,\"pin\":\"4040\",\"confirm_risk\":true,\"expected_event_sequence\":4294967295}")==200);
 assert(phase_test_active&&event_log.sequence==0&&!writes);
}
int main(void){
 decision_clock_interleave();
 phase_revision();
 /* Neither normal offset nor HTTP control trials may be interrupted by a
    competing contactor test; cancelling remains possible in both cases. */
 for(unsigned trial=0;trial<2;trial++){
  fixture();normal_offset_trial.armed=trial==0;shell_http_trial.armed=trial==1;
  assert(request(one)==409&&!writes&&!phase_test_active&&phase_state==PHASE_READY);
  phase_test_active=true;
  assert(request("{\"stop\":true}")==200&&!phase_test_active&&!settings.enabled&&!writes);
  assert(normal_offset_trial.armed==(trial==0)&&shell_http_trial.armed==(trial==1));
 }
 fixture();assert(phase_config_gate_locked(clock_ms)==CONFIG_GATE_OK);assert(strstr(phase_config_gate_message(CONFIG_GATE_CURRENT),"1 A"));
 (void)phase_mqtt_setup_locked;(void)phase_mqtt_take_locked;(void)phase_mqtt_sent_locked;(void)phase_output_health_locked;(void)phase_shelly_receive_locked;
 fixture();assert(phase_mqtt_stopped_locked());actual_at=meter_at=0;assert(phase_mqtt_stopped_locked());
 fixture();phase_state=PHASE_MOVING;phase_startup=true;settings.phase_actuator_type=1;phase_shelly_command_at=0;assert(phase_config_quiescent_locked());phase_shelly_inflight=true;assert(!phase_config_quiescent_locked());phase_shelly_inflight=false;phase_shelly_command_at=1;assert(!phase_config_quiescent_locked());
 fixture();phase_config_saving=true;phase_step_locked(clock_ms);assert(!writes);phase_config_saving=false;
 fixture();phase_state=PHASE_FAULT;assert(phase_test_ready_locked(clock_ms));
 /* An isolated contactor test must work without Shell polling; the actual
    meter still supplies a new zero sample throughout the hold. */
 wallbox_at=last_sent_current_at=0;evcc_control.configured=true;assert(phase_test_ready_locked(clock_ms));assert(request(one)==200&&!writes);
 for(int64_t t=2000;t<=16000;t+=1000){clock_ms=actual_at=meter_at=t;phase_step_locked(t);}
 assert(writes==1&&phase_state==PHASE_READY&&settings.charge_phases==1&&!settings.enabled);
 fixture();phase_state=PHASE_STOPPING;phase_goal=1;phase_stop_at=1000;wallbox_at=last_sent_current_at=0;
 for(int64_t t=2000;t<=16000;t+=1000){clock_ms=actual_at=meter_at=t;phase_step_locked(t);}
 assert(!writes&&phase_state==PHASE_STOPPING); /* automatic path retains Shell interlock */
 fixture();settings.phase_actuator_type=1;phase_state=PHASE_FAULT;phase_shelly_config_ok=false;
 assert(strstr(phase_test_block_locked(clock_ms),"Shelly-Verbindung"));assert(request(one)==409&&!writes);
 fixture();auth=false;assert(request(one)==401&&!writes&&!phase_test_active);auth=true;
 assert(request("{\"phases\":1,\"pin\":\"4040\",\"confirm_risk\":false}")==400);
 assert(request("{\"phases\":1,\"phases\":3,\"pin\":\"4040\",\"confirm_risk\":true}")==400);
 assert(request("{\"phases\":1,\"pin\":\"0000\",\"confirm_risk\":true}")==400);assert(request(one)==429);expert_wrong_until=0;
 settings.enabled=true;assert(request(one)==409);settings.enabled=false;evcc_control.enabled=true;assert(request(one)==409);evcc_control.enabled=false;
 actual_a[1]=1;assert(request(one)==409);actual_a[1]=0;actual_at=0;assert(request(one)==409);actual_at=1000;
 pending=true;assert(request(one)==409);pending=false;
 assert(request(one)==200&&phase_test_active&&phase_state==PHASE_STOPPING&&!settings.enabled);
 assert(request(one)==409);for(int64_t t=2000;t<=9000;t+=1000)step(t,0);assert(writes==0);
 step(10000,0);assert(writes==1&&phase_state==PHASE_MOVING);step(11000,0);step(16000,0);assert(phase_state==PHASE_READY&&settings.charge_phases==1);
 for(int64_t t=17000;t<=24000;t+=1000)step(t,0);assert(writes==1&&!settings.enabled);
 assert(request("{\"stop\":true}")==200&&!phase_test_active&&!settings.enabled);
 fixture();assert(request(one)==200);phase_test_tick_locked(90999);assert(phase_test_active);phase_test_tick_locked(91000);assert(!phase_test_active&&phase_state==PHASE_FAULT&&!settings.enabled);
 fixture();assert(request(one)==200);for(int64_t t=2000;t<=6000;t+=1000)step(t,0);step(7000,1.2f);assert(!phase_zero_at&&!writes);for(int64_t t=8000;t<=15000;t+=1000)step(t,0);assert(!writes);step(16000,0);assert(writes==1);
 fixture();phase_state=PHASE_FAULT;assert(phase_fault_restart_safe_locked(clock_ms));actual_a[2]=1;assert(!phase_fault_restart_safe_locked(clock_ms));actual_a[2]=0;clock_ms=8000;assert(!phase_fault_restart_safe_locked(clock_ms));actual_at=meter_at=0;assert(phase_fault_restart_safe_locked(clock_ms));relay_on[0]=true;assert(!phase_fault_restart_safe_locked(clock_ms));
 fixture();settings.phase_actuator_type=1;phase_state=PHASE_STOPPING;phase_startup=false;phase_shelly_config_ok=false;
 assert(phase_fault_restart_safe_locked(clock_ms));phase_shelly_inflight=true;assert(!phase_fault_restart_safe_locked(clock_ms));phase_shelly_inflight=false;
 actual_a[1]=1;assert(!phase_fault_restart_safe_locked(clock_ms));actual_a[1]=0;actual_at=clock_ms-6000;assert(!phase_fault_restart_safe_locked(clock_ms));
 fixture();settings.phase_actuator_type=1;phase_state=PHASE_FAULT;phase_shelly_config_ok=true;phase_shelly_at=clock_ms;phase_shelly_feedback_value=(shelly_phase_feedback_t){0};assert(phase_test_ready_locked(clock_ms));
 
 phase_shelly_inflight=true;assert(!phase_test_ready_locked(clock_ms)&&!phase_fault_restart_safe_locked(clock_ms));phase_shelly_inflight=false;
 assert(request(one)==200);phase_stop_at=1000;phase_zero_at=2000;clock_ms=actual_at=meter_at=wallbox_at=last_sent_current_at=11000;phase_shelly_at=11000;phase_step_locked(clock_ms);assert(phase_state==PHASE_MOVING&&phase_shelly_pending);
 assert(phase_shelly_take_command_locked(clock_ms));assert(phase_shelly_inflight&&!phase_shelly_take_command_locked(clock_ms));phase_shelly_ack_locked(false);assert(!phase_shelly_inflight&&phase_state==PHASE_FAULT);phase_step_locked(clock_ms);assert(!phase_test_active&&!settings.enabled&&!phase_shelly_pending);
 /* Feedback already reports the target, but output disagrees. An explicit
    absolute test must still write once; a bistable toggle must stay blocked. */
 fixture();settings.phase_actuator_type=1;phase_state=PHASE_FAULT;
 phase_shelly_config_ok=true;phase_shelly_at=clock_ms;
 phase_shelly_feedback_value=(shelly_phase_feedback_t){.output=false};
 assert(phase_test_ready_locked(clock_ms));
 
 settings.phase_shelly_pulse=false;phase_shelly_feedback_value.output=false;
 assert(request(one)==200);
 phase_stop_at=1000;phase_zero_at=2000;clock_ms=actual_at=meter_at=wallbox_at=last_sent_current_at=11000;phase_shelly_at=11000;
 phase_step_locked(clock_ms);assert(phase_state==PHASE_MOVING&&phase_shelly_pending);
 assert(phase_shelly_take_command_locked(clock_ms));
 assert(phase_shelly_inflight&&!phase_shelly_take_command_locked(clock_ms));
 phase_shelly_ack_locked(true);phase_shelly_feedback_value.output=true;phase_shelly_at=12000;
 step(12000,0);phase_shelly_at=17000;step(17000,0);
 assert(phase_state==PHASE_READY&&settings.charge_phases==1&&!settings.enabled);
 fixture();settings.phase_feedback_source=0;settings.phase_feedback_pin=13;settings.phase_feedback_count=2;
 settings_fixed_pins(&settings);assert(settings.phase_feedback_source==2&&!settings.phase_feedback_enabled&&settings.phase_feedback_pin==-1);
 puts("PASS: stopped fault recovery, retired-contact normalization, authenticated hardware test, zero-current hold, current rise, single command, in-flight protection and no charge start");
}
