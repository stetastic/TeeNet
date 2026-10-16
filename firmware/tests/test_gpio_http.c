#include "ems_core.h"
static bool phase_test_active;
#include "hardware_profile.h"
#include "cJSON.h"
#include <assert.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
typedef int esp_err_t;
#define ESP_OK 0
#define ESP_FAIL 1
#define pdTRUE 1
#define HTTPD_400_BAD_REQUEST 400
#define HTTPD_500_INTERNAL_SERVER_ERROR 500
typedef struct {int status;char message[200];} httpd_req_t;
enum {PHASE_READY,PHASE_STOPPING,PHASE_MOVING,PHASE_SETTLING,PHASE_FAULT};
static settings_t settings,saved_settings,persisted;
static bool boot_validation_pending;
static bool ota_in_progress,restarting,backup_in_progress,evcc_configuring,reboot_required,phase_config_saving;
static bool phase_startup,phase_shelly_pending,phase_shelly_inflight,phase_mqtt_pending,phase_mqtt_inflight,phase_mqtt_known,relay_on[2];
static int64_t phase_shelly_command_at,phase_mqtt_sent_at;
static struct {bool configured,enabled,inhibited;} evcc_control;
static int phase_state,locks,writes,save_error,maintenance_flash_lock,race_start;
static int64_t actual_at,meter_at;
static float actual_a[3];
#define LOCK() assert(locks++==0)
#define UNLOCK() assert(--locks==0)
static int64_t now_ms(void){return 10000;}
static int64_t wallbox_meter_ttl(void){return 5000;}
static bool phase_shelly_remote(void){return settings.phase_actuator_type==1||settings.phase_actuator_type==3;}
static bool phase_mqtt_remote(void){return settings.phase_actuator_type==2;}
static unsigned ticks(unsigned ms){return ms;}
static bool restart_pending(void){return reboot_required;}
static int xSemaphoreTake(int sem,unsigned ms){(void)sem;(void)ms;if(race_start)settings.enabled=true;return pdTRUE;}
static void xSemaphoreGive(int sem){(void)sem;}
static void event_locked(const char *event){(void)event;}
static esp_err_t save_config(const settings_t *next){writes++;assert(reboot_required);if(!save_error)persisted=*next;return save_error;}
static void httpd_resp_set_status(httpd_req_t *req,const char *text){req->status=atoi(text);}
static esp_err_t httpd_resp_sendstr(httpd_req_t *req,const char *text){snprintf(req->message,sizeof(req->message),"%s",text);return 0;}
static esp_err_t httpd_resp_send_err(httpd_req_t *req,int code,const char *text){req->status=code;return httpd_resp_sendstr(req,text);}
static esp_err_t send_json(httpd_req_t *req,cJSON *o){req->status=200;cJSON_Delete(o);return 0;}
#include "phase_config_guard.inc"
#include "gpio_http.inc"
static void fixture(void){settings_defaults(&settings);saved_settings=settings;phase_state=PHASE_READY;actual_at=meter_at=0;memset(actual_a,0,sizeof(actual_a));memset(&evcc_control,0,sizeof(evcc_control));writes=save_error=race_start=locks=0;reboot_required=ota_in_progress=restarting=backup_in_progress=evcc_configuring=false;}
static httpd_req_t post(const char *json){httpd_req_t req={0};cJSON *o=cJSON_Parse(json);assert(o);assert(gpio_post(&req,o)==0&&!locks);return req;}
int main(void){
 (void)phase_config_gate_locked;(void)phase_config_gate_message;
 fixture();httpd_req_t r=post("{\"values\":{\"xemex_tx_pin\":8,\"xemex_rx_pin\":9,\"phase_feedback_pin\":15}} ");
 assert(r.status==200&&writes==1&&reboot_required);assert(saved_settings.xemex_tx_pin==8&&settings.xemex_tx_pin==4&&persisted.phase_feedback_pin==15);
 cJSON *snap=gpio_snapshot();assert(cJSON_GetArraySize(cJSON_GetObjectItem(snap,"roles"))==12&&cJSON_GetArraySize(cJSON_GetObjectItem(snap,"pins"))==49);cJSON_Delete(snap);
 fixture();settings.enabled=true;r=post("{\"values\":{\"xemex_tx_pin\":8}}");assert(r.status==409&&!writes);
 fixture();actual_at=1000;r=post("{\"values\":{\"xemex_tx_pin\":8}}");assert(r.status==409&&!writes);
 fixture();actual_at=9000;actual_a[0]=1.1;r=post("{\"values\":{\"xemex_tx_pin\":8}}");assert(r.status==409&&!writes);
 fixture();race_start=1;r=post("{\"values\":{\"xemex_tx_pin\":8}}");assert(r.status==409&&!writes&&!reboot_required);
 fixture();save_error=ESP_FAIL;r=post("{\"values\":{\"xemex_tx_pin\":8}}");assert(r.status==500&&writes==1&&!reboot_required&&saved_settings.xemex_tx_pin==4);
 const char *bad[]={"{\"values\":{\"relay1_pin\":0}}","{\"values\":{\"xemex_tx_pin\":17}}","{\"values\":{\"xemex_tx_pin\":8.5}}","{\"values\":{\"xemex_tx_pin\":8,\"xemex_tx_pin\":9}}","{\"values\":{\"wifi_password\":1}}","{\"values\":{}}"};
 for(unsigned i=0;i<sizeof(bad)/sizeof(*bad);i++){fixture();r=post(bad[i]);assert(r.status==400&&!writes&&!reboot_required);}
 fixture();phase_state=PHASE_FAULT;r=post("{\"values\":{\"phase_feedback_pin\":15}}");assert(r.status==200&&writes==1);
 fixture();settings.phase_actuator_type=saved_settings.phase_actuator_type=1;phase_state=PHASE_MOVING;phase_startup=true;actual_at=9000;
 r=post("{\"values\":{\"phase_feedback_pin\":15}}");assert(r.status==200&&writes==1);
 fixture();phase_config_saving=true;r=post("{\"values\":{\"phase_feedback_pin\":15}}");assert(r.status==409&&!writes);phase_config_saving=false;
 fixture();phase_state=PHASE_MOVING;phase_startup=false;r=post("{\"values\":{\"phase_feedback_pin\":15}}");assert(r.status==409&&!writes);
 fixture();phase_state=PHASE_MOVING;phase_startup=true;phase_shelly_inflight=true;r=post("{\"values\":{\"phase_feedback_pin\":15}}");assert(r.status==409&&!writes);phase_shelly_inflight=false;phase_startup=false;
 puts("PASS: actual GPIO endpoint: first setup, no live remap, commit reservation, active/stale/current locks, write failure rollback, conflicting/duplicate/unknown values and two-step stop recheck");
 return 0;
}
