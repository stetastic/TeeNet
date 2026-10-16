#include "ems_core.h"
static bool phase_test_active;
#include "cJSON.h"
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
typedef int esp_err_t;typedef struct {int status;const char *body;} httpd_req_t;
#define ESP_OK 0
#define ESP_FAIL -1
#define ESP_ERR_TIMEOUT 1
#define ESP_ERR_INVALID_STATE 2
#define HTTPD_400_BAD_REQUEST 400
#define HTTPD_403_FORBIDDEN 403
#define pdTRUE 1
#define ticks(x) (x)
static settings_t settings,saved_settings,persisted;
static bool phase_mqtt_allowed,phase_mqtt_saving,config_storage_ok=true,stopped=true,pending,reboot_required;
static bool send_busy,flash_busy,start_during_wait;static int writes,error,locks,send_locked,flash_locked,setups;
static int phase_mqtt_send_lock=1,maintenance_flash_lock=2;
#define LOCK() assert(locks++==0)
#define UNLOCK() assert(--locks==0)
static bool restart_pending(void){return pending||reboot_required;}
static bool phase_mqtt_remote(void){return settings.phase_actuator_type==2;}
static void phase_mqtt_cancel_locked(bool stop){assert(locks==1);if(stop&&phase_mqtt_remote()){settings.enabled=false;settings.mode=MODE_OFF;}}
static bool phase_mqtt_stopped_locked(void){assert(locks==1);return stopped&&!settings.enabled;}
static int64_t now_ms(void){return 1000;}
static void phase_mqtt_setup_locked(int64_t now){(void)now;setups++;}
static int xSemaphoreTake(int lock,int wait){(void)wait;assert(!locks);
    if(lock==phase_mqtt_send_lock){if(send_busy)return 0;send_locked=1;}else{if(flash_busy)return 0;assert(send_locked);flash_locked=1;if(start_during_wait)settings.enabled=true;}return pdTRUE;}
static void xSemaphoreGive(int lock){assert(!locks);if(lock==phase_mqtt_send_lock)send_locked=0;else flash_locked=0;}
static esp_err_t save_config(const settings_t *next){assert(!locks&&send_locked&&flash_locked&&phase_mqtt_saving);writes++;if(error)return ESP_FAIL;persisted=*next;return ESP_OK;}
static esp_err_t httpd_resp_send_err(httpd_req_t *r,int status,const char *msg){(void)msg;r->status=status;return ESP_OK;}
static void httpd_resp_set_status(httpd_req_t *r,const char *status){r->status=atoi(status);}
static esp_err_t httpd_resp_sendstr(httpd_req_t *r,const char *text){(void)r;(void)text;return ESP_OK;}
static esp_err_t send_json(httpd_req_t *r,cJSON *o){cJSON_Delete(o);r->status=200;return ESP_OK;}
static esp_err_t ok_json(httpd_req_t *r){cJSON *o=cJSON_CreateObject();cJSON_AddBoolToObject(o,"ok",true);return send_json(r,o);}
#include "phase_mqtt_storage.inc"
#include "phase_mqtt_http.inc"
static int64_t expert_wrong_until;
static bool local_authorized=true;
static bool authorized(httpd_req_t *req){if(!local_authorized)req->status=401;return local_authorized;}
static cJSON *read_json(httpd_req_t *req){return cJSON_Parse(req->body);}
static cJSON *expert_values_snapshot(void){return cJSON_CreateObject();}
static cJSON *gpio_snapshot(void){return cJSON_CreateObject();}
static bool boot_validation_pending;
static esp_err_t gpio_post(httpd_req_t *req,cJSON *o){(void)req;cJSON_Delete(o);assert(0);return 0;}
#include "phase_auth_actual.inc"
static int root_request(const char *body){httpd_req_t req={0,body};expert_post(&req);return req.status;}
static int request(const char *body){httpd_req_t req={0};cJSON *json=cJSON_Parse(body);assert(json);phase_mqtt_post(&req,json);return req.status;}
static void boot_saved(void){
    settings=saved_settings;reboot_required=false;pending=false;
    phase_mqtt_allowed=settings.phase_mqtt_output_enabled;
    if(phase_mqtt_allowed&&phase_mqtt_remote())phase_mqtt_setup_locked(now_ms());
}
int main(void){
    settings_defaults(&settings);settings.mqtt_enabled=true;settings.xemex_coils=3;saved_settings=settings;
    const char *yes="{\"enabled\":true,\"confirm_risk\":true,\"confirm_no_feedback\":true,\"repeat_pin\":\"4040\",\"topic\":\"wallbox-ems/output/single_phase\",\"true_three_phase\":false}";
    const char *no="{\"enabled\":true,\"confirm_risk\":true,\"confirm_no_feedback\":true,\"repeat_pin\":\"4040\",\"topic\":\"wallbox-ems/output/single_phase\",\"true_three_phase\":true}";
    assert(request("{\"enabled\":true}")==400&&!writes);
    assert(request("{\"enabled\":true,\"confirm_risk\":true,\"confirm_no_feedback\":true,\"repeat_pin\":\"4040\",\"topic\":\"test/set\"}")==400&&!writes);
    assert(request("{\"enabled\":true,\"confirm_risk\":true,\"confirm_no_feedback\":true,\"repeat_pin\":\"4040\",\"topic\":\"test/set\",\"true_three_phase\":\"true\"}")==400&&!writes);
    assert(request("{\"enabled\":true,\"confirm_risk\":true,\"confirm_no_feedback\":true,\"repeat_pin\":\"0000\",\"topic\":\"test/set\"}")==400&&!writes);
    assert(request("{\"enabled\":true,\"enabled\":false,\"confirm_risk\":true,\"confirm_no_feedback\":true,\"repeat_pin\":\"4040\",\"topic\":\"test/set\"}")==400&&!writes);
    stopped=false;assert(request(yes)==409&&!writes);stopped=true;
    start_during_wait=true;assert(request(yes)==409&&!writes);start_during_wait=false;settings.enabled=false;
    error=1;assert(request(yes)==500&&!phase_mqtt_allowed);error=0;
    assert(request(yes)==200&&!phase_mqtt_allowed&&persisted.phase_mqtt_output_enabled&&persisted.phase_actuator_type==2);
    assert(!persisted.phase_feedback_enabled&&reboot_required&&!phase_mqtt_saving);
    boot_saved();assert(phase_mqtt_allowed&&setups==1);settings.enabled=true;
    send_busy=true;assert(request("{\"enabled\":false}")==500&&!phase_mqtt_allowed&&!settings.enabled);send_busy=false;
    phase_mqtt_allowed=true;settings.enabled=true;error=1;
    assert(request("{\"enabled\":false}")==500&&!phase_mqtt_allowed&&!settings.enabled);error=0;
    assert(request("{\"enabled\":false}")==200&&!phase_mqtt_allowed&&!persisted.phase_mqtt_output_enabled&&persisted.phase_actuator_type==0&&persisted.phase_feedback_enabled);
    boot_saved();
    settings.enabled=false;assert(request(yes)==200&&!phase_mqtt_allowed&&reboot_required);
    boot_saved();assert(phase_mqtt_allowed&&setups==2);
    int count=writes;assert(request(no)==409&&writes==count&&!settings.phase_mqtt_true_three_phase);
    assert(request("{\"enabled\":false}")==200&&!phase_mqtt_allowed&&persisted.phase_actuator_type==0);
    boot_saved();
    assert(request(no)==200&&!phase_mqtt_allowed&&reboot_required&&persisted.phase_mqtt_true_three_phase);
    boot_saved();assert(phase_mqtt_allowed&&settings.phase_mqtt_true_three_phase&&setups==3);
    assert(request(yes)==409);
    assert(request("{\"enabled\":false,\"true_three_phase\":false}")==200&&persisted.phase_mqtt_true_three_phase);boot_saved();
    error=1;assert(request(yes)==500&&!phase_mqtt_allowed&&settings.phase_mqtt_true_three_phase);error=0;
    assert(request(yes)==200&&!phase_mqtt_allowed&&reboot_required&&!persisted.phase_mqtt_true_three_phase);boot_saved();
    assert(phase_mqtt_allowed&&setups==4);
    assert(request("{\"enabled\":false}")==200);boot_saved();
    const char *rpc="{\"enabled\":true,\"confirm_risk\":true,\"confirm_no_feedback\":true,\"repeat_pin\":\"4040\",\"true_three_phase\":false,\"transport\":2,\"host\":\"192.168.1.88\",\"switch_id\":0}";
    assert(request(rpc)==200&&reboot_required&&persisted.phase_output_transport==2&&!strcmp(persisted.phase_output_host,"192.168.1.88"));
    boot_saved();
    assert(request("{\"enabled\":false}")==200);boot_saved();assert(request(rpc)==200&&reboot_required);boot_saved();
    settings_t migrated;assert(settings_decode(&persisted,sizeof(persisted),&migrated)&&migrated.phase_output_transport==2&&!strcmp(migrated.phase_output_host,"192.168.1.88"));
    assert(request("{\"enabled\":true,\"confirm_risk\":true,\"confirm_no_feedback\":true,\"repeat_pin\":\"4040\",\"true_three_phase\":false,\"transport\":1,\"host\":\"192.168.1.88\"}")==409);
    config_storage_ok=false;settings.enabled=true;
    assert(request("{\"enabled\":false}")==500&&!phase_mqtt_allowed&&!settings.enabled);
    assert(!locks&&!send_locked&&!flash_locked&&!phase_mqtt_saving);

    config_storage_ok=true;settings.enabled=false;
    assert(request("{\"enabled\":false}")==200);boot_saved();
    const char *protected_rpc="{\"enabled\":true,\"confirm_risk\":true,\"confirm_no_feedback\":true,\"repeat_pin\":\"4040\",\"true_three_phase\":false,\"transport\":3,\"host\":\"192.168.1.88\",\"switch_id\":0,\"password\":\"synthetic-secret\"}";
    assert(request(protected_rpc)==200&&persisted.phase_output_transport==3&&!strcmp(persisted.phase_output_password,"synthetic-secret"));
    boot_saved();
    assert(settings_decode(&persisted,sizeof(persisted),&migrated)&&!strcmp(migrated.phase_output_password,"synthetic-secret"));
    assert(request("{\"enabled\":false}")==200);boot_saved();
    assert(request("{\"enabled\":true,\"confirm_risk\":true,\"confirm_no_feedback\":true,\"repeat_pin\":\"4040\",\"true_three_phase\":false,\"transport\":3,\"host\":\"192.168.1.88\",\"switch_id\":0}")==200);boot_saved();
    assert(request("{\"enabled\":true,\"confirm_risk\":true,\"confirm_no_feedback\":true,\"repeat_pin\":\"4040\",\"true_three_phase\":false,\"transport\":3,\"host\":\"192.168.1.88\",\"password\":\"changed-secret\"}")==409);
    assert(request("{\"enabled\":false}")==200);boot_saved();
    assert(request("{\"enabled\":true,\"confirm_risk\":true,\"confirm_no_feedback\":true,\"repeat_pin\":\"4040\",\"true_three_phase\":false,\"transport\":3,\"host\":\"192.168.1.99\"}")==409);
    assert(request(rpc)==200&&!persisted.phase_output_password[0]);boot_saved();
    phase_mqtt_allowed=true;settings.enabled=true;
    count=writes;assert(root_request("{\"action\":\"mqtt_phase_output\",\"enabled\":true}")==403&&writes==count);
    assert(expert_wrong_until>now_ms());
    assert(root_request("{\"action\":\"mqtt_phase_output\",\"enabled\":false}")==200&&!phase_mqtt_allowed&&!settings.enabled);
    phase_mqtt_allowed=true;settings.enabled=true;local_authorized=false;count=writes;
    assert(root_request("{\"action\":\"mqtt_phase_output\",\"enabled\":false}")==401&&writes==count&&phase_mqtt_allowed);
    local_authorized=true;expert_wrong_until=0;
    assert(root_request("{\"action\":\"mqtt_phase_output\",\"enabled\":false,\"enabled\":true}")==400&&writes==count&&phase_mqtt_allowed);
    assert(root_request("{\"pin\":\"4040\",\"action\":\"read\"}")==200);
    assert(root_request("{\"pin\":\"4040\",\"action\":\"read_gpio\"}")==200);
    assert(root_request("{\"pin\":\"4040\",\"action\":\"authorize\"}")==200);
    assert(root_request("{\"action\":\"mqtt_phase_output\",\"enabled\":false}")==200&&!phase_mqtt_allowed);
    settings_defaults(&settings);settings.mqtt_enabled=true;settings.xemex_coils=3;saved_settings=settings;
    phase_mqtt_allowed=false;reboot_required=false;pending=true;stopped=true;count=writes;
    assert(request(yes)==200&&writes==count+1&&!phase_mqtt_allowed&&reboot_required);
    assert(persisted.phase_mqtt_output_enabled&&persisted.phase_actuator_type==2);
    settings_defaults(&settings);settings.mqtt_enabled=true;settings.xemex_coils=3;saved_settings=settings;
    phase_mqtt_allowed=false;reboot_required=false;pending=false;stopped=true;
    count=writes;assert(request("{\"enabled\":true,\"confirm_risk\":true,\"confirm_no_feedback\":true,\"repeat_pin\":\"4040\",\"true_three_phase\":false,\"transport\":1,\"host\":\"192.168.1.88\",\"switch_id\":0,\"shelly_model\":-1}")==400&&writes==count);
    count=writes;assert(request("{\"enabled\":true,\"confirm_risk\":true,\"confirm_no_feedback\":true,\"repeat_pin\":\"4040\",\"true_three_phase\":false,\"transport\":1,\"host\":\"192.168.1.88\",\"switch_id\":0,\"shelly_model\":0}")==400&&writes==count);
    count=writes;assert(request("{\"enabled\":true,\"confirm_risk\":true,\"confirm_no_feedback\":true,\"repeat_pin\":\"4040\",\"true_three_phase\":false,\"transport\":1,\"host\":\"192.168.1.88\",\"switch_id\":0,\"shelly_model\":3}")==400&&writes==count);
    count=writes;assert(request("{\"enabled\":true,\"confirm_risk\":true,\"confirm_no_feedback\":true,\"repeat_pin\":\"4040\",\"true_three_phase\":false,\"transport\":1,\"host\":\"192.168.1.88\",\"switch_id\":0,\"shelly_model\":13}")==400&&writes==count);
    count=writes;assert(request("{\"enabled\":true,\"confirm_risk\":true,\"confirm_no_feedback\":true,\"repeat_pin\":\"4040\",\"true_three_phase\":false,\"transport\":1,\"host\":\"192.168.1.88\",\"switch_id\":0,\"shelly_model\":4.5}")==400&&writes==count);
    count=writes;assert(request("{\"enabled\":true,\"confirm_risk\":true,\"confirm_no_feedback\":true,\"repeat_pin\":\"4040\",\"true_three_phase\":false,\"transport\":1,\"host\":\"192.168.1.88\",\"switch_id\":0,\"shelly_model\":\"4\"}")==400&&writes==count);
    assert(request("{\"enabled\":true,\"confirm_risk\":true,\"confirm_no_feedback\":true,\"repeat_pin\":\"4040\",\"true_three_phase\":false,\"transport\":1,\"host\":\"192.168.1.88\",\"switch_id\":0,\"shelly_model\":4}")==200&&persisted.phase_shelly_model==4);boot_saved();
    assert(settings.phase_shelly_model==4);
    puts("PASS: real expert output API and root authorization: guarded activation, PIN-free local revocation even after wrong PIN, no unauthorized revoke, duplicate rejection, transports and persistence");
}
