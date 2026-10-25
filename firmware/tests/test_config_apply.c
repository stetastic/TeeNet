#include "ems_core.h"
static bool phase_test_active,phase_config_saving,phase_startup,phase_shelly_pending,phase_shelly_inflight,phase_mqtt_pending,phase_mqtt_inflight,phase_mqtt_known,relay_on[1];
static int64_t phase_shelly_command_at,phase_mqtt_sent_at;

#include "cJSON.h"
#include <assert.h>
#include <limits.h>
#include <math.h>
#include <stdio.h>
#include <string.h>

typedef int esp_err_t;
#define ESP_OK 0
#define HTTPD_400_BAD_REQUEST 400
#define HTTPD_403_FORBIDDEN 403
#define HTTPD_500_INTERNAL_SERVER_ERROR 500
typedef struct { int status; char message[240]; } httpd_req_t;
enum { PHASE_READY, PHASE_STOPPING, PHASE_MOVING, PHASE_SETTLING, PHASE_FAULT };
static settings_t settings,saved_settings,prepared_settings;
static bool boot_validation_pending;
static bool ota_in_progress,restarting,evcc_configuring;
static struct { bool configured,enabled,inhibited; } evcc_control;
static int phase_state,prepared_count,locks;
static int64_t actual_at,meter_at;
static float actual_a[3];
#define LOCK() assert(locks++==0)
#define UNLOCK() assert(--locks==0)
static int64_t now_ms(void){return 1000;}
static int64_t wallbox_meter_ttl(void){return 5000;}
static esp_err_t httpd_resp_send_err(httpd_req_t *req,int code,const char *message){req->status=code;snprintf(req->message,sizeof(req->message),"%s",message);return ESP_OK;}
static void httpd_resp_set_status(httpd_req_t *req,const char *status){req->status=!strncmp(status,"409",3)?409:500;}
static esp_err_t httpd_resp_sendstr(httpd_req_t *req,const char *message){snprintf(req->message,sizeof(req->message),"%s",message);return ESP_OK;}
/* House HTTP query construction is unrelated to these actuator fixtures. */
static bool house_query_url(const char *type,const char *host,const char *path,char *out,size_t size){(void)type;(void)host;(void)path;(void)out;(void)size;return true;}
static bool phase_shelly_remote(void){return settings.phase_actuator_type==1||settings.phase_actuator_type==3;}
static bool phase_mqtt_remote(void){return settings.phase_actuator_type==2;}
static bool fail_save;
static esp_err_t save_config(const settings_t *s){(void)s;assert(!locks);return fail_save?-1:ESP_OK;}
#include "phase_config_guard.inc"
#include "config_apply_actual.inc"

static void fixture(void){
    settings_defaults(&settings);saved_settings=settings;phase_state=PHASE_READY;
    memset(&evcc_control,0,sizeof(evcc_control));memset(actual_a,0,sizeof(actual_a));
    actual_at=meter_at=0;prepared_count=locks=0;
    phase_config_saving=phase_startup=phase_shelly_pending=phase_shelly_inflight=phase_mqtt_pending=phase_mqtt_inflight=phase_mqtt_known=relay_on[0]=fail_save=false;phase_shelly_command_at=phase_mqtt_sent_at=0;
}
static httpd_req_t post(const char *body){
    httpd_req_t req={0};cJSON *o=cJSON_Parse(body);assert(o);
    assert(config_apply(&req,o,false)==ESP_OK && !locks);
    return req;
}
int main(void){
    char body[640];
    fixture();phase_state=PHASE_MOVING;phase_startup=true;settings.phase_actuator_type=1;saved_settings=settings;actual_at=meter_at=500;
    httpd_req_t waiting=post("{\"phase_shelly_host\":\"192.168.1.88\"}");assert(waiting.status==200&&!phase_config_saving);
    fixture();phase_state=PHASE_MOVING;phase_startup=false;settings.phase_actuator_type=1;saved_settings=settings;actual_at=meter_at=500;
    waiting=post("{\"phase_shelly_host\":\"192.168.1.88\"}");assert(waiting.status==400&&strstr(waiting.message,"Schaltvorgang"));
    fixture();phase_state=PHASE_MOVING;phase_startup=true;phase_shelly_inflight=true;settings.phase_actuator_type=1;saved_settings=settings;actual_at=meter_at=500;
    waiting=post("{\"phase_shelly_host\":\"192.168.1.88\"}");assert(waiting.status==400&&!prepared_count);
    fixture();actual_at=-5000;meter_at=500;
    waiting=post("{\"phase_shelly_host\":\"192.168.1.88\"}");assert(waiting.status==400&&strstr(waiting.message,"veraltet"));
    fixture();actual_at=meter_at=500;fail_save=true;
    waiting=post("{\"phase_shelly_host\":\"192.168.1.88\"}");assert(waiting.status==500&&!phase_config_saving&&!prepared_count);
    fixture();phase_state=PHASE_FAULT;
    httpd_req_t recovery=post("{\"basic_mode\":false,\"relay_board_enabled\":true,\"phase_switch_enabled\":true,\"phase_actuator_type\":1,\"phase_shelly_model\":4,\"phase_shelly_host\":\"192.168.1.88\",\"xemex_coils\":2}");
    assert(recovery.status==200&&prepared_settings.phase_switch_enabled&&prepared_settings.phase_actuator_type==1);
    fixture();phase_state=PHASE_FAULT;actual_at=meter_at=500;
    recovery=post("{\"phase_feedback_count\":1,\"phase_feedback_closed_is_single\":true}");
    assert(recovery.status==200&&prepared_settings.phase_feedback_closed_is_single);
    fixture();phase_state=PHASE_FAULT;settings.enabled=true;actual_at=meter_at=500;
    recovery=post("{\"phase_feedback_closed_is_single\":true}");assert(recovery.status!=200&&!prepared_count);
    fixture();phase_state=PHASE_FAULT;actual_at=meter_at=500;actual_a[1]=1.2f;
    recovery=post("{\"phase_feedback_closed_is_single\":true}");assert(recovery.status!=200&&!prepared_count);
    fixture();
    cJSON *backup=cJSON_Parse("{\"price_kwh\":0.30,\"phase_feedback_count\":2,\"wifi_password_set\":true,\"phase_shelly_password_set\":false,\"phase_mqtt_output_enabled\":true,\"phase_output_transport\":3,\"phase_output_password_set\":true}");
    config_remove_readonly(backup);assert(cJSON_GetArraySize(backup)==2);
    assert(!cJSON_GetObjectItem(backup,"phase_mqtt_output_enabled"));
    httpd_req_t restored={0};config_apply(&restored,backup,true);
    assert(restored.status==200&&prepared_count==1&&prepared_settings.phase_feedback_count==2);
    assert(!prepared_settings.phase_mqtt_output_enabled);
    for(unsigned kind=1;kind<=3;kind+=2){
        for(unsigned coils=2;coils<=3;coils++){
            fixture();
            snprintf(body,sizeof(body),"{\"basic_mode\":false,\"relay_board_enabled\":true,\"phase_switch_enabled\":true,\"phase_actuator_type\":%u,\"phase_shelly_model\":%u,\"phase_shelly_host\":\"192.168.1.88\",\"xemex_coils\":%u,\"phase_feedback_count\":%u}",kind,kind==3?4:0,coils,kind==3?1:2);
            httpd_req_t req=post(body);
            if(req.status!=200){fprintf(stderr,"Valid setup rejected: %s\n",req.message);assert(0);}
            assert(prepared_count==1 && prepared_settings.xemex_coils==coils);
            assert(!strcmp(prepared_settings.phase_shelly_host,"192.168.1.88"));
            assert(prepared_settings.phase_feedback_count==(kind==3?1:2));
            saved_settings=prepared_settings;
            req=post("{\"price_kwh\":0.30}");assert(req.status==200 && prepared_count==2);
        }
    }
    /* The protected output profile survives unrelated settings saves. If an
       ordinary setting removes its MQTT prerequisite, it is revoked and the
       visible normal feedback profile is restored instead of becoming an
       unsavable half-configuration. */
    fixture();settings.basic_mode=false;settings.relay_board_enabled=true;settings.phase_switch_enabled=true;
    settings.phase_actuator_type=2;settings.phase_feedback_enabled=false;settings.phase_mqtt_output_enabled=true;
    settings.mqtt_enabled=true;settings.xemex_coils=2;saved_settings=settings;
    recovery=post("{\"price_kwh\":0.31}");
    assert(recovery.status==200&&prepared_settings.phase_actuator_type==2&&prepared_settings.phase_mqtt_output_enabled);
    saved_settings=prepared_settings;settings=saved_settings;
    recovery=post("{\"mqtt_enabled\":false}");
    assert(recovery.status==200&&prepared_settings.phase_actuator_type==0&&prepared_settings.phase_feedback_enabled&&!prepared_settings.phase_mqtt_output_enabled);
    /* An unrelated malformed field must name its own error, not claim that
       the valid host/coils later in the request were missing. */
    fixture();httpd_req_t bad=post("{\"basic_mode\":false,\"relay_board_enabled\":true,\"phase_switch_enabled\":true,\"phase_actuator_type\":1,\"mqtt_enabled\":\"true\",\"phase_shelly_host\":\"192.168.1.88\",\"xemex_coils\":2}");
    assert(bad.status==400 && !prepared_count);
    assert(strstr(bad.message,"mqtt_enabled")&&!strstr(bad.message,"Shelly"));
    fixture();bad=post("{\"basic_mode\":false,\"relay_board_enabled\":true,\"phase_switch_enabled\":true,\"phase_actuator_type\":1,\"unknown_parameter\":0,\"phase_shelly_host\":\"192.168.1.88\",\"xemex_coils\":2}");
    assert(bad.status==400&&!prepared_count&&strstr(bad.message,"unknown_parameter")&&!strstr(bad.message,"Shelly"));
    fixture();bad=post("{\"phase_actuator_type\":0,\"phase_actuator_type\":1}");
    assert(bad.status==400&&!prepared_count&&strstr(bad.message,"phase_actuator_type"));
    fixture();bad=post("[]");assert(bad.status==400&&!prepared_count);
    fixture();bad=post("{\"relay_active_low\":true}");
    assert(bad.status==200&&prepared_count==1&&prepared_settings.relay_active_low);
    prepared_settings.basic_mode=false;prepared_settings.relay_active_low=true;
    settings_t migrated;assert(settings_decode(&prepared_settings,sizeof(prepared_settings),&migrated));assert(migrated.relay_active_low);
    fixture();settings.enabled=true;actual_at=meter_at=500;
    bad=post("{\"relay_active_low\":true}");assert(bad.status==400&&!prepared_count);
    fixture();actual_at=meter_at=500;actual_a[1]=1.1f;
    bad=post("{\"relay_active_low\":true}");assert(bad.status==400&&!prepared_count);
    fixture();actual_at=-5000;meter_at=500;
    bad=post("{\"relay_active_low\":true}");assert(bad.status==400&&!prepared_count);
    fixture();bad=post("{\"basic_mode\":false,\"relay_board_enabled\":true,\"phase_switch_enabled\":true,\"phase_actuator_type\":1,\"xemex_coils\":2}");
    assert(bad.status==400&&!prepared_count&&strstr(bad.message,"Shelly-Adresse fehlt")&&!strstr(bad.message,"L1"));
    fixture();bad=post("{\"basic_mode\":false,\"relay_board_enabled\":true,\"phase_switch_enabled\":true,\"phase_actuator_type\":1,\"phase_shelly_host\":\"192.168.1.88\",\"xemex_coils\":1}");
    assert(bad.status==400&&!prepared_count&&strstr(bad.message,"1 Messspule")&&!strstr(bad.message,"Adresse"));
    fixture();bad=post("{\"basic_mode\":false,\"relay_board_enabled\":true,\"phase_switch_enabled\":true,\"phase_actuator_type\":1,\"phase_shelly_host\":\"192.168.1.88\",\"wallbox_meter_type\":\"shelly_em1\",\"wallbox_meter_host\":\"192.168.1.89\"}");
    assert(bad.status==400&&!prepared_count&&strstr(bad.message,"shelly_em1")&&!strstr(bad.message,"Adresse"));
    printf("Malformed unrelated field is now reported explicitly, with no partial save.\n");
    puts("PASS: actual ESP parser accepts valid Shelly HTTP/Modbus with two or three Xemex coils and retained settings; no hardware or flash writes");
}
