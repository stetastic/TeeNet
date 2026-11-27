#include "telemetry_core.h"
#include <assert.h>
#include <stdio.h>
#include <time.h>

#define TELEMETRY_ENDPOINT "fixture"
#define VERSION "1"
#define EMS_BUILD_ID "test"
#define EMS_HARDWARE_ID "atom"
#define pdPASS 1
#define pdTRUE 1
#define ESP_OK 0
#define HTTP_METHOD_DELETE 1
#define HTTP_METHOD_POST 2
#define PHASE_READY 0
#define MALLOC_CAP_INTERNAL 1
#define MALLOC_CAP_8BIT 2
static int64_t clock_ms=100000,actual_at=100000,evcc_shell_at=100000;
static int64_t telemetry_last_at,telemetry_next_at;
static const char *telemetry_wait_reason;
static int telemetry_last_http;
static uint32_t telemetry_duration_ms;
static bool telemetry_last_ok,locked,send_locked,lock_busy,fail_start,store_failed;
static bool terms_accepted=true,wifi_online=true,ota_in_progress,restarting,backup_in_progress,boot_validation_pending;
static bool phase_config_saving,phase_startup,phase_shelly_pending,phase_shelly_inflight,phase_shelly_rechecking;
static bool phase_mqtt_pending,phase_mqtt_inflight,phase_test_active;
static int phase_state,telemetry_lock=1;
static char shell_active_host[1];
static struct {bool enabled,phase_switch_enabled,shell_limits_auto,shell_http_control_enabled;char shell_setup_host[1];} settings;
static telemetry_idle_window_t telemetry_meter_idle,telemetry_shell_idle;
static struct {bool enabled,configured;} evcc_control;
typedef struct {char state,raw[32];struct {bool present;} session;} fake_shell_t;
static fake_shell_t evcc_shell={.state='A'};
static float actual_a[3];
static telemetry_config_t telemetry_config={.schema=1};
static size_t available=150000,largest=65536;
static unsigned attempts,releases,requests,writes,method;
static int result=200;
static void (*pending)(void *);
#define LOCK() do {assert(!locked);locked=true;} while(0)
#define UNLOCK() do {assert(locked);locked=false;} while(0)
static int64_t now_ms(void){return clock_ms;}
static int wallbox_meter_ttl(void){return 30000;}
static bool fresh(int64_t now,int64_t sample,int64_t ttl){return now>=sample&&now-sample<ttl;}
static bool shell_vehicle_fresh(const fake_shell_t *s,int64_t sample,int64_t now){return s&&fresh(now,sample,30000);}
static size_t heap_caps_get_free_size(int caps){assert(caps==3&&!locked);return available;}
static size_t heap_caps_get_largest_free_block(int caps){assert(caps==3&&!locked);return largest;}
static unsigned esp_random(void){return 1;}
static int xSemaphoreTake(int lock,int delay){assert(lock&&!delay&&!send_locked);if(lock_busy)return 0;send_locked=true;return pdTRUE;}
static void xSemaphoreGive(int lock){assert(lock&&send_locked);send_locked=false;}
static int xTaskCreate(void (*entry)(void *),const char *name,unsigned bytes,void *arg,unsigned priority,void *handle){
    assert(!locked&&!send_locked&&!pending&&!arg&&!handle&&priority==2&&bytes==6144&&!strcmp(name,"device_stats"));
    attempts++;
    if(fail_start)return 0;
    pending=entry;return pdPASS;
}
static void vTaskDelete(void *self){assert(!self&&!locked&&!send_locked&&pending);pending=NULL;releases++;}
static bool telemetry_store(const telemetry_config_t *c){assert(send_locked&&!c->enabled&&!c->id[0]);writes++;return !store_failed;}
typedef int esp_err_t,esp_http_client_handle_t;
typedef struct {const char *url;int method,timeout_ms,crt_bundle_attach,buffer_size,buffer_size_tx;bool disable_auto_redirect;} esp_http_client_config_t;
#define esp_crt_bundle_attach 1
static esp_http_client_handle_t esp_http_client_init(const esp_http_client_config_t *c){
    assert(send_locked&&!locked&&c->crt_bundle_attach&&c->disable_auto_redirect);
    method=c->method;return 1;
}
static void esp_http_client_set_header(int client,const char *name,const char *value){assert(client&&name[0]&&value[0]);}
static void esp_http_client_set_post_field(int client,const char *body,size_t bytes){
    assert(client&&bytes==strlen(body)&&strstr(body,telemetry_config.id));
    assert((strstr(body,"hardware")!=NULL)==(method==HTTP_METHOD_POST));
}
static esp_err_t esp_http_client_perform(int client){assert(client&&send_locked);requests++;clock_ms+=850;return ESP_OK;}
static int esp_http_client_get_status_code(int client){assert(client);return result;}
static void esp_http_client_cleanup(int client){assert(client);}
#include "telemetry_worker.inc"

static void advance(void){clock_ms+=TELEMETRY_INTERVAL_MS+15000;actual_at=evcc_shell_at=clock_ms;}
int main(void){
    telemetry_schedule();assert(!attempts);  /* opt out */
    telemetry_config.enabled=1;strcpy(telemetry_config.revision,TELEMETRY_REVISION);
    memset(telemetry_config.id,'a',32);memset(telemetry_config.key,'b',64);
    available=TELEMETRY_MIN_INTERNAL_FREE+TELEMETRY_STACK_BYTES-1;
    telemetry_schedule();assert(!attempts);
    available++;settings.enabled=true;telemetry_schedule();assert(!attempts);
    settings.enabled=false;fail_start=true;telemetry_schedule();assert(attempts==1&&!telemetry_worker_running);
    fail_start=false;telemetry_schedule();telemetry_schedule();assert(attempts==2&&pending);
    largest=TELEMETRY_MIN_INTERNAL_BLOCK-1;pending(NULL);assert(!requests&&!telemetry_next_at);
    largest++;telemetry_schedule();lock_busy=true;pending(NULL);lock_busy=false;assert(!requests&&!telemetry_next_at);
    telemetry_schedule();settings.enabled=true;pending(NULL);settings.enabled=false;assert(!requests);
    telemetry_schedule();terms_accepted=false;pending(NULL);terms_accepted=true;assert(!requests);
    evcc_shell.state='B';evcc_shell.session.present=true;strcpy(evcc_shell.raw,"idle");
    largest=31744;
    for(unsigned i=0;i<1000;i++){
        advance();telemetry_schedule();assert(pending);pending(NULL);
        assert(requests==i+1&&method==HTTP_METHOD_POST&&telemetry_last_ok&&telemetry_last_at==clock_ms);
        assert(telemetry_duration_ms==850&&telemetry_last_http==200&&!telemetry_wait_reason);
        telemetry_schedule();assert(!pending&&!telemetry_worker_running);
    }
    advance();evcc_control.enabled=true;evcc_control.configured=true;
    evcc_shell.state='B';strcpy(evcc_shell.raw,"SuspendedEV");
    for(int seconds=0;seconds<=300;seconds++){
        int64_t previous=actual_at;
        clock_ms+=1000;actual_at=evcc_shell_at=clock_ms;
        telemetry_idle_sample(&telemetry_meter_idle,previous,actual_at,wallbox_meter_ttl(),true,true);
        telemetry_schedule();assert((pending!=NULL)==(seconds==300));
    }
    phase_test_active=true;pending(NULL);phase_test_active=false;assert(requests==1000);
    telemetry_schedule();actual_a[1]=6;pending(NULL);assert(requests==1000);
    actual_a[1]=0;telemetry_meter_idle.since=0;
    evcc_control.enabled=false;settings.enabled=true;
    settings.shell_http_control_enabled=true;actual_at=0;
    for(int seconds=0;seconds<=300;seconds++){
        int64_t previous=evcc_shell_at;
        clock_ms+=1000;evcc_shell_at=clock_ms;
        telemetry_idle_sample(&telemetry_shell_idle,previous,evcc_shell_at,30000,true,true);
        telemetry_schedule();assert((pending!=NULL)==(seconds==300));
    }
    pending(NULL);assert(requests==1001&&method==HTTP_METHOD_POST);
    settings.enabled=false;settings.shell_http_control_enabled=false;strcpy(evcc_shell.raw,"idle");
    advance();telemetry_revoke(&telemetry_config);terms_accepted=false;
    result=503;telemetry_schedule();pending(NULL);assert(!telemetry_last_ok&&telemetry_config.delete_pending&&!writes);
    result=200;advance();store_failed=true;telemetry_schedule();pending(NULL);
    assert(telemetry_config.delete_pending&&writes==1);
    advance();store_failed=false;telemetry_schedule();pending(NULL);
    assert(method==HTTP_METHOD_DELETE&&!telemetry_config.delete_pending&&writes==2&&!telemetry_config.key[0]);
    advance();telemetry_schedule();assert(!pending&&releases+1==attempts);
    puts("PASS: optional statistics releases task after 1000 sends; consent, deletion/retry, pressure, concurrent changes and charging gates preserved");
}
