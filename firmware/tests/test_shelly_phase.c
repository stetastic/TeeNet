#include "ems_core.h"
#include "shelly_phase.h"
#include <assert.h>
#include <stdio.h>
#include <string.h>
#include <math.h>
#include <stdlib.h>
#include <errno.h>
#include <fcntl.h>

static settings_t settings;
typedef enum {PHASE_READY,PHASE_STOPPING,PHASE_MOVING,PHASE_SETTLING,PHASE_FAULT} phase_state_t;
static phase_state_t phase_state;
static uint8_t phase_goal,phase_candidate;
static int64_t phase_candidate_at,phase_stop_at,phase_zero_at,phase_move_at,phase_hold_until,phase_attempt_at,phase_no_load_since;
static bool phase_startup,phase_idle_inhibit,phase_gpio_ready=true,restarting,ota_in_progress,wifi_online=true,relay_on[1];
static float actual_a[3];
static int64_t actual_at,meter_at,wallbox_at,last_sent_current_at;
static struct {bool configured,enabled,preparing,inhibited;unsigned phases;} evcc_control;
static struct {float target_a;} pv_control;
static current_calibration_t manual_filter;
static unsigned pulses;
static int64_t wallbox_meter_ttl(void){return 5000;}
static void phase_mqtt_guard_locked(void){}
static bool phase_mqtt_remote(void){return false;}
static bool phase_output_link_locked(void){return false;}
static bool phase_mqtt_motion_complete_locked(uint8_t goal,int64_t now){(void)goal;(void)now;return false;}
static unsigned evcc_desired_phases(const void *unused,unsigned active){(void)unused;return evcc_control.enabled?evcc_control.phases:active;}
static float phase_surplus_w_locked(int64_t now){(void)now;return 3300;}
static bool phase_test_active;static bool phase_config_saving,reboot_required;static uint8_t phase_test_goal;
static void phase_test_end_locked(void){phase_test_active=false;}
#include "phase_voltage.h"
static phase_voltage_proof_t phase_voltage;
static unsigned phase_voltage_observed_locked(int64_t now){return wifi_online?phase_voltage_observed(&phase_voltage,now):0;}
#include "phase_shelly_runtime.inc"
static unsigned gpio_reads;
static int64_t now_ms(void){return meter_at;}
#define PHASE_FEEDBACK_GPIO settings.phase_feedback_pin
#define PHASE_FEEDBACK_SECOND_GPIO settings.phase_feedback_second_pin
/* Deliberately contradict the Shelly's three-phase contacts. */
static int gpio_get_level(int pin){gpio_reads++;return pin==PHASE_FEEDBACK_GPIO?1:0;}
#include "phase_observed_actual.inc"
static bool phase_actuator_begin_locked(uint8_t goal,int64_t now){return phase_shelly_begin_locked(goal,now);}
static float vehicle_phase_surplus_locked(float w){return w;}
#include "phase_controller.inc"

static shelly_phase_feedback_t feedback(unsigned phases,bool output,uint32_t uptime){
    (void)phases;return (shelly_phase_feedback_t){.output=output,.uptime=uptime,.has_uptime=true};
}
static void fixture(unsigned phases){
    settings_defaults(&settings);settings.phase_actuator_type=1;settings.phase_feedback_count=2;settings.phase_feedback_closed_is_single=false;settings.phase_shelly_pulse=false;strcpy(settings.phase_shelly_host,"192.168.1.88");
    settings.phase_feedback_source=1;strcpy(settings.phase_voltage_host,"192.168.1.89");settings.xemex_coils=3;settings.phase_switch_enabled=settings.relay_board_enabled=settings.phase_feedback_enabled=true;
    settings.relay1_mode=3;settings.charge_phases=phases;settings.manual_phases=phases;
    settings.mode=MODE_MANUAL;settings.enabled=true;
    phase_state=PHASE_READY;phase_goal=phase_candidate=phases;relay_on[0]=phases==1;
    phase_candidate_at=phase_stop_at=phase_zero_at=phase_move_at=phase_hold_until=phase_attempt_at=phase_no_load_since=0;
    phase_startup=phase_idle_inhibit=restarting=ota_in_progress=false;phase_gpio_ready=wifi_online=true;
    phase_shelly_at=phase_shelly_config_at=1000;phase_shelly_command_at=0;phase_shelly_config_ok=true;
    phase_shelly_pending=phase_shelly_uncertain=phase_shelly_restart_prepared=phase_shelly_have_uptime=false;
    phase_shelly_rechecking=false;phase_shelly_recheck_at=0;
    memset(&phase_voltage,0,sizeof(phase_voltage));float volts[3]={230,phases==3?230:0,phases==3?230:0};phase_voltage_sample(&phase_voltage,volts,true,500);phase_voltage_sample(&phase_voltage,volts,true,1000);phase_shelly_uptime=0;phase_shelly_feedback_value=feedback(phases,false,100);pulses=0;
    memset(&evcc_control,0,sizeof(evcc_control));memset(&pv_control,0,sizeof(pv_control));
}
static void step(int64_t now,float a1,float a2,unsigned observed,bool output){
    actual_at=meter_at=wallbox_at=last_sent_current_at=now;actual_a[0]=a1;actual_a[1]=a2;actual_a[2]=0;
    float volts[3]={230,observed==3?230:0,observed==3?230:0};phase_voltage_sample(&phase_voltage,volts,true,now-500);phase_voltage_sample(&phase_voltage,volts,true,now);
    shelly_phase_feedback_t status=feedback(observed,output,100+(uint32_t)(now/1000));
    assert(phase_shelly_receive_locked(&status,true,now));phase_step_locked(now);
    if(phase_shelly_take_command_locked(now)){
        assert(a1<ems_param(EP_PHASE_ZERO_A)&&a2<ems_param(EP_PHASE_ZERO_A));
        pulses++;phase_shelly_ack_locked(true);
    }
}


/* Exercise the actual network worker without any real I/O. A test response
   script drives one iteration; the first sleep returns to the test harness. */
typedef struct {char *data;size_t capacity;bool overflow;} http_body_t;
typedef struct {
    const char *url,*username,*password;int timeout_ms,auth_type,max_authorization_retries;
    bool disable_auto_redirect;void (*event_handler)(void);void *user_data;
} esp_http_client_config_t;
typedef struct {int code,error,socket_error;bool complete,overflow;const char *body;} fake_reply_t;
typedef void *esp_http_client_handle_t;
typedef int esp_err_t;
#define ESP_OK 0
#define ESP_ERR_HTTP_CONNECT 0x7002
#define ESP_ERR_HTTP_WRITE_DATA 0x7003
#define ESP_ERR_HTTP_FETCH_HEADER 0x7004
#define ESP_ERR_HTTP_INVALID_TRANSPORT 0x7005
#define ESP_ERR_HTTP_EAGAIN 0x7007
#define ESP_ERR_HTTP_CONNECTION_CLOSED 0x7008
#define ESP_ERR_INVALID_ARG 0x102
#define ESP_ERR_TIMEOUT 0x107
#define ESP_ERR_NO_MEM 0x101
#define HTTP_AUTH_TYPE_DIGEST 1
#define HTTP_AUTH_TYPE_NONE 0
static fake_reply_t replies[4];static unsigned reply_count,reply_index,http_calls,status_calls,coil_calls;
static esp_http_client_config_t fake_client;
static void *worker_buffer;static unsigned lock_depth;static int slept;
#define LWIP_SOCKET_OFFSET 48
#define CONFIG_LWIP_MAX_SOCKETS 16
#define MALLOC_CAP_INTERNAL 1
#define MALLOC_CAP_8BIT 2
static unsigned socket_mask,socket_probes,heap_probes;
static int fake_fcntl(int fd,int cmd,int value){
    assert(!lock_depth&&cmd==F_GETFL&&value==0);
    assert(fd>=LWIP_SOCKET_OFFSET&&fd<LWIP_SOCKET_OFFSET+CONFIG_LWIP_MAX_SOCKETS);
    socket_probes++;
    if(socket_mask&(1u<<(fd-LWIP_SOCKET_OFFSET))){errno=0;return 0;}
    errno=EBADF;return -1;
}
static size_t fake_heap_free(unsigned caps){
    assert(!lock_depth&&reply_index<reply_count&&caps==(MALLOC_CAP_INTERNAL|MALLOC_CAP_8BIT));
    heap_probes++;return 65000;
}
static size_t fake_heap_largest(unsigned caps){
    assert(!lock_depth&&reply_index<reply_count&&caps==(MALLOC_CAP_INTERNAL|MALLOC_CAP_8BIT));
    heap_probes++;return 29000;
}
static uint32_t boot_task_seen[5];
static void http_body_event(void){}
static esp_http_client_handle_t esp_http_client_init(const esp_http_client_config_t *c){
    assert(reply_index<reply_count);fake_client=*c;http_calls++;
    if(strstr(c->url,"GetStatus"))status_calls++;
    return &fake_client;
}
static esp_err_t esp_http_client_perform(esp_http_client_handle_t client){
    assert(client==&fake_client);const fake_reply_t *r=&replies[reply_index];
    http_body_t *body=fake_client.user_data;size_t size=strlen(r->body?r->body:"");assert(size<body->capacity);
    memcpy(body->data,r->body?r->body:"",size+1);body->overflow=r->overflow;return r->error;
}
static int esp_http_client_get_status_code(esp_http_client_handle_t c){(void)c;return replies[reply_index].code;}
static int esp_http_client_get_errno(esp_http_client_handle_t c){(void)c;assert(reply_index<reply_count);return replies[reply_index].socket_error;}
static bool esp_http_client_is_complete_data_received(esp_http_client_handle_t c){(void)c;return replies[reply_index].complete;}
static void esp_http_client_cleanup(esp_http_client_handle_t c){(void)c;reply_index++;}
static void *worker_malloc(size_t size){assert(!worker_buffer);worker_buffer=malloc(size);assert(worker_buffer);return worker_buffer;}
#define vTaskDelay(delay) do{assert(!lock_depth);slept=delay;return;}while(0)
#define vTaskDelete(task) do{(void)(task);assert(0);return;}while(0)
#define ticks(x) (x)
#define LOCK() assert(lock_depth++==0)
#define UNLOCK() assert(--lock_depth==0)
#define malloc worker_malloc
#define fcntl fake_fcntl
#define heap_caps_get_free_size fake_heap_free
#define heap_caps_get_largest_free_block fake_heap_largest
#include "phase_shelly_network.inc"
#undef heap_caps_get_largest_free_block
#undef heap_caps_get_free_size
#undef fcntl
#undef malloc
#undef vTaskDelay
#undef vTaskDelete
#undef LOCK
#undef UNLOCK
static bool phase_output_coil(const char *host,unsigned id,bool writing,bool *on){
    (void)host;(void)id;(void)writing;(void)on;coil_calls++;return false;
}
static const char *valid_identity="{\"id\":\"shellypro1-test\",\"gen\":2,\"model\":\"SPSW\",\"auth_en\":false}";
static const char *valid_config="{\"switch:0\":{\"id\":0,\"in_mode\":\"detached\",\"initial_state\":\"off\",\"auto_on\":false,\"auto_off\":false}}";
static const char *valid_status="{\"switch:0\":{\"id\":0,\"output\":false},\"sys\":{\"uptime\":102}}";
static fake_reply_t good(const char *body){return (fake_reply_t){.code=200,.complete=true,.body=body};}
static void worker_fixture(int64_t now){
    fixture(3);settings.phase_shelly_model=0;phase_shelly_inflight=false;
    actual_at=meter_at=wallbox_at=last_sent_current_at=now;
    actual_a[0]=actual_a[1]=actual_a[2]=0;
    reply_count=reply_index=http_calls=status_calls=coil_calls=0;lock_depth=0;slept=0;
    phase_shelly_transport_misses=phase_shelly_invalid_replies=0;phase_shelly_last_read_failure_at=0;
    phase_shelly_http_diag_valid=phase_shelly_failure_http_valid=false;
    phase_shelly_http_resources_valid=phase_shelly_failure_resources_valid=false;
    socket_mask=0;socket_probes=heap_probes=0;
}
static void worker_once(void){
    phase_shelly_task(NULL);
    assert(!lock_depth&&reply_index==reply_count);free(worker_buffer);worker_buffer=NULL;
}
static void add_reply(fake_reply_t r){assert(reply_count<4);replies[reply_count++]=r;}
static void valid_config_replies(void){add_reply(good(valid_identity));add_reply(good(valid_config));}
static void test_socket_diagnostics(void){
    /* Include holes and both ends of the SDK's socket-only descriptor range.
       A successful F_GETFL may return zero, so it must still count. */
    worker_fixture(2000);socket_mask=(1u<<0)|(1u<<7)|(1u<<15);errno=EAGAIN;
    assert(phase_shelly_socket_count()==3&&socket_probes==16&&errno==EAGAIN&&!heap_probes);
    socket_mask=0;errno=ECONNREFUSED;assert(phase_shelly_socket_count()==0&&errno==ECONNREFUSED);
    socket_mask=0xffff;errno=0;assert(phase_shelly_socket_count()==16&&errno==0);
    worker_fixture(2000);socket_mask=0xffff;valid_config_replies();
    add_reply((fake_reply_t){.error=ESP_ERR_HTTP_CONNECT,.complete=true});worker_once();
    assert(socket_probes==16&&heap_probes==2&&phase_shelly_failure_resources_valid);
    assert(phase_shelly_failure_open_sockets==16&&phase_shelly_failure_socket_capacity==16);
    assert(phase_shelly_failure_internal_free==65000&&phase_shelly_failure_internal_largest==29000);
    /* Successful requests neither census sockets nor erase the prior failure. */
    reply_count=reply_index=0;valid_config_replies();add_reply(good(valid_status));worker_once();
    assert(socket_probes==16&&heap_probes==2&&phase_shelly_failure_resources_valid);
    assert(phase_shelly_failure_open_sockets==16&&!phase_shelly_http_resources_valid);
    /* A new application-level payload failure has no transport census; do
       not accidentally attach the old failed request's resource sample. */
    reply_count=reply_index=0;valid_config_replies();add_reply(good("{}"));worker_once();
    assert(!phase_shelly_failure_resources_valid&&socket_probes==16&&heap_probes==2);
    puts("PASS: failure-only socket census covers SDK FD bounds/gaps, preserves errno, uses internal heap, runs before cleanup/outside lock and retains only matching diagnostics");
}
static void test_transport_missing(void){
    /* Keep exactly the original valid status. Neither a failed request nor a
       loop of failed requests advances its deadline or proves a command. */
    fixture(3);phase_shelly_transport_missing_locked(2000);
    assert(phase_state==PHASE_READY&&phase_shelly_at==1000&&!phase_shelly_rechecking);
    phase_shelly_transport_missing_locked(5999);assert(phase_state==PHASE_READY&&phase_shelly_at==1000);
    phase_shelly_transport_missing_locked(6000);assert(phase_state==PHASE_READY&&phase_shelly_at==1000);
    phase_shelly_transport_missing_locked(6001);assert(phase_state==PHASE_SETTLING&&phase_shelly_rechecking&&!phase_shelly_at);
    fixture(3);phase_shelly_uncertain=true;phase_shelly_transport_missing_locked(2000);assert(phase_state==PHASE_FAULT);
    fixture(3);wifi_online=false;phase_shelly_transport_missing_locked(2000);assert(phase_state==PHASE_SETTLING);
    fixture(3);phase_state=PHASE_MOVING;phase_goal=3;phase_shelly_command_at=1500;
    phase_shelly_transport_missing_locked(2000);assert(!phase_shelly_motion_complete_locked(3,2000)&&phase_shelly_at==1000);
    fixture(3);phase_shelly_transport_missing_locked(2000);step(2500,0,0,3,true);assert(phase_state==PHASE_FAULT);
    fixture(3);phase_shelly_have_uptime=true;phase_shelly_uptime=200;
    shelly_phase_feedback_t reboot=feedback(3,false,2);
    assert(!phase_shelly_receive_locked(&reboot,true,2000)&&phase_state==PHASE_FAULT);

    /* Configuration transport failure must not allow a status refresh. */
    worker_fixture(2000);add_reply((fake_reply_t){.error=ESP_ERR_HTTP_EAGAIN});worker_once();
    assert(phase_shelly_config_ok&&phase_shelly_config_at==1000&&phase_shelly_at==1000&&phase_state==PHASE_READY&&status_calls==0&&slept==500);
    assert(phase_shelly_transport_misses==1&&!phase_shelly_invalid_replies&&phase_shelly_last_read_failure_at==2000);
    worker_fixture(5999);add_reply((fake_reply_t){.error=ESP_ERR_HTTP_EAGAIN});worker_once();assert(phase_shelly_at==1000&&phase_state==PHASE_READY);
    worker_fixture(6001);add_reply((fake_reply_t){.error=ESP_ERR_HTTP_EAGAIN});worker_once();assert(phase_state==PHASE_SETTLING&&!phase_shelly_at&&status_calls==0);
    worker_fixture(2000);add_reply(good(valid_identity));add_reply((fake_reply_t){.error=ESP_ERR_HTTP_EAGAIN});worker_once();
    assert(phase_shelly_config_at==1000&&phase_shelly_at==1000&&status_calls==0);
    worker_fixture(2000);add_reply(good("{}"));worker_once();assert(!phase_shelly_config_ok&&phase_state==PHASE_FAULT&&status_calls==0);
    worker_fixture(2000);add_reply(good(valid_identity));add_reply(good("{}"));worker_once();assert(!phase_shelly_config_ok&&phase_state==PHASE_FAULT);
    worker_fixture(2000);add_reply((fake_reply_t){.code=401,.complete=true});worker_once();assert(phase_state==PHASE_FAULT);

    worker_fixture(2000);valid_config_replies();add_reply((fake_reply_t){.error=ESP_ERR_HTTP_EAGAIN});worker_once();
    assert(phase_state==PHASE_READY&&phase_shelly_at==1000&&status_calls==1&&!coil_calls);
    worker_fixture(2000);valid_config_replies();add_reply((fake_reply_t){.code=200,.complete=false,.body="{"});worker_once();assert(phase_state==PHASE_READY&&phase_shelly_at==1000);
    worker_fixture(2000);valid_config_replies();fake_reply_t oversized=good(valid_status);oversized.overflow=true;add_reply(oversized);worker_once();assert(phase_state==PHASE_SETTLING&&!phase_shelly_at);
    worker_fixture(2000);valid_config_replies();add_reply(good("{}"));worker_once();assert(phase_state==PHASE_SETTLING&&!phase_shelly_at);
    worker_fixture(2000);valid_config_replies();add_reply((fake_reply_t){.code=403,.complete=true});worker_once();assert(phase_state==PHASE_SETTLING&&!phase_shelly_at);
    worker_fixture(2000);valid_config_replies();add_reply((fake_reply_t){.code=401,.error=ESP_ERR_HTTP_EAGAIN});worker_once();
    assert(phase_state==PHASE_SETTLING&&!phase_shelly_at&&phase_shelly_invalid_replies==1&&!phase_shelly_transport_misses);
    worker_fixture(2000);valid_config_replies();add_reply((fake_reply_t){.error=ESP_ERR_HTTP_EAGAIN,.socket_error=11});worker_once();
    assert(phase_shelly_failure_http_valid&&phase_shelly_failure_http_error==ESP_ERR_HTTP_EAGAIN&&phase_shelly_failure_http_status==0&&phase_shelly_failure_http_errno==11);
    /* A successful retry updates current evidence without erasing the last
       failure diagnostics. No error state is fetched after client cleanup. */
    reply_count=reply_index=0;valid_config_replies();add_reply(good(valid_status));worker_once();
    assert(phase_shelly_http_error==ESP_OK&&phase_shelly_http_status==200&&phase_shelly_failure_http_error==ESP_ERR_HTTP_EAGAIN&&phase_shelly_failure_http_errno==11);
    const int transport_errors[]={ESP_ERR_HTTP_CONNECT,ESP_ERR_HTTP_WRITE_DATA,ESP_ERR_HTTP_EAGAIN,ESP_ERR_HTTP_CONNECTION_CLOSED,ESP_ERR_TIMEOUT,ESP_ERR_NO_MEM};
    for(unsigned i=0;i<sizeof(transport_errors)/sizeof(*transport_errors);i++){
        worker_fixture(2000);valid_config_replies();add_reply((fake_reply_t){.error=transport_errors[i]});worker_once();
        assert(phase_state==PHASE_READY&&phase_shelly_at==1000&&phase_shelly_transport_misses==1&&!phase_shelly_invalid_replies);
        /* Exact IDF pre-header quirk: zero bytes expected and received makes
           is_complete_data_received true even when connecting failed. */
        worker_fixture(2000);valid_config_replies();add_reply((fake_reply_t){.error=transport_errors[i],.code=0,.complete=true});worker_once();
        assert(phase_state==PHASE_READY&&phase_shelly_at==1000&&phase_shelly_transport_misses==1&&!phase_shelly_invalid_replies);
    }
    worker_fixture(2000);add_reply((fake_reply_t){.error=ESP_ERR_HTTP_CONNECT,.code=0,.complete=true});worker_once();
    assert(phase_shelly_config_at==1000&&phase_shelly_at==1000&&phase_state==PHASE_READY&&!status_calls&&phase_shelly_transport_misses==1);
    worker_fixture(6001);add_reply((fake_reply_t){.error=ESP_ERR_HTTP_CONNECT,.code=0,.complete=true});worker_once();
    assert(phase_state==PHASE_SETTLING&&!phase_shelly_at&&!status_calls&&phase_shelly_transport_misses==1);
    worker_fixture(2000);valid_config_replies();add_reply((fake_reply_t){.error=ESP_ERR_HTTP_CONNECT,.code=401,.complete=true});worker_once();
    assert(phase_state==PHASE_SETTLING&&!phase_shelly_at&&phase_shelly_invalid_replies==1&&!phase_shelly_transport_misses);
    const int invalid_errors[]={ESP_ERR_HTTP_FETCH_HEADER,ESP_ERR_HTTP_INVALID_TRANSPORT,ESP_ERR_INVALID_ARG,-1};
    for(unsigned i=0;i<sizeof(invalid_errors)/sizeof(*invalid_errors);i++){
        worker_fixture(2000);valid_config_replies();add_reply((fake_reply_t){.error=invalid_errors[i]});worker_once();
        assert(phase_state==PHASE_SETTLING&&!phase_shelly_at&&phase_shelly_invalid_replies==1&&!phase_shelly_transport_misses);
    }
    worker_fixture(2000);valid_config_replies();add_reply((fake_reply_t){.code=200,.error=ESP_ERR_HTTP_EAGAIN,.complete=true,.body="{}"});worker_once();
    assert(phase_state==PHASE_SETTLING&&!phase_shelly_at&&phase_shelly_invalid_replies==1);
    worker_fixture(2000);valid_config_replies();add_reply(good(valid_status));worker_once();assert(phase_state==PHASE_READY&&phase_shelly_at==2000);
    worker_fixture(2000);settings.phase_actuator_type=3;valid_config_replies();worker_once();assert(coil_calls==1&&phase_state==PHASE_SETTLING&&!phase_shelly_at&&!phase_shelly_failure_http_valid);
    puts("PASS: actual phase HTTP worker distinguishes missing transport from invalid payload/config; original 5 s deadline unchanged, no stale configuration/status renewal, no command confirmation or Modbus relaxation");
}

int main(void){
    (void)gpio_get_level;
    shelly_phase_identity_t identity;
    assert(shelly_phase_identity("{\"id\":\"shelly1g4-abcd\",\"gen\":4,\"model\":\"S4SW\",\"auth_en\":false}",&identity)&&identity.profile==4);
    assert(!shelly_phase_identity("{\"id\":\"other\",\"gen\":4,\"model\":\"S4SW\",\"auth_en\":false}",&identity));
    const char *config="{\"switch:0\":{\"id\":0,\"in_mode\":\"detached\",\"initial_state\":\"off\",\"auto_on\":false,\"auto_off\":false}}";
    assert(shelly_phase_config(config));assert(!shelly_phase_config("{}"));
    assert(!shelly_phase_config("{\"switch:0\":{\"id\":0,\"in_mode\":\"detached\",\"initial_state\":\"off\",\"auto_on\":false,\"auto_off\":true,\"auto_off_delay\":0.5}}"));
    shelly_phase_feedback_t parsed;
    assert(shelly_phase_feedback("{\"switch:0\":{\"id\":0,\"output\":true},\"sys\":{\"uptime\":100}}",&parsed)&&parsed.output);
    assert(!shelly_phase_feedback("{\"switch:0\":{\"id\":0,\"output\":true,\"errors\":[\"overload\"]},\"sys\":{\"uptime\":100}}",&parsed));
    assert(!shelly_phase_feedback("{\"switch:0\":{\"id\":0,\"output\":true}}",&parsed));
    char path[96];assert(shelly_output_path(path,sizeof(path),"Set",0,true));
    assert(strstr(path,"on=true")&&!strstr(path,"toggle"));
    /* Absolute ESC226 outputs: one verified command per change, no toggle pulse. */
    fixture(3);settings.phase_feedback_source=2;settings.phase_feedback_enabled=false;settings.manual_phases=1;
    assert(settings_valid(&settings));gpio_reads=0;
    for(int64_t now=1000;now<=26000;now+=1000)step(now,now<3000?10:0,0,pulses?1:3,pulses>0);
    assert(pulses==1&&phase_state==PHASE_READY&&settings.charge_phases==1&&gpio_reads==0);
    settings.manual_phases=3;
    for(int64_t now=27000;now<=52000;now+=1000)step(now,0,0,pulses<2?1:3,pulses<2);
    assert(pulses==2&&phase_state==PHASE_READY&&settings.charge_phases==3);
    /* Valid voltage samples must be newer than the actual command. */
    fixture(1);phase_shelly_feedback_value.output=true;phase_shelly_command_at=10000;phase_shelly_at=12000;
    float single[3]={230,0,0};memset(&phase_voltage,0,sizeof(phase_voltage));
    phase_voltage_sample(&phase_voltage,single,true,9000);phase_voltage_sample(&phase_voltage,single,true,9500);
    assert(!phase_shelly_motion_complete_locked(1,12000));
    phase_voltage_sample(&phase_voltage,single,true,12000);assert(phase_shelly_motion_complete_locked(1,12000));
    wifi_online=false;assert(!phase_shelly_motion_complete_locked(1,12000));wifi_online=true;
    /* Command/status contradiction and lost links block charging; no silent resends. */
    fixture(1);settings.phase_feedback_source=2;settings.phase_feedback_enabled=false;phase_shelly_feedback_value.output=true;
    step(2000,8,8,1,true);assert(phase_state==PHASE_FAULT&&!pulses);
    fixture(3);settings.phase_feedback_source=2;settings.phase_feedback_enabled=false;wifi_online=false;
    phase_shelly_guard_locked(1000);assert(phase_state==PHASE_SETTLING&&phase_shelly_rechecking&&!pulses);
    /* A lost read pauses without a write; matching fresh samples recover a
       stopped controller. No automatic Start or speculative phase change. */
    fixture(3);settings.enabled=false;
    assert(!phase_shelly_receive_locked(NULL,false,2000));assert(phase_shelly_rechecking&&phase_state==PHASE_SETTLING);
    for(int64_t t=2500;t<=10000;t+=500)step(t,0,0,3,false);
    assert(phase_state==PHASE_READY&&!phase_shelly_rechecking&&!settings.enabled&&!pulses);
    fixture(3);assert(!phase_shelly_receive_locked(NULL,false,2000));
    for(int64_t t=3000;t<=33000;t+=1000){phase_shelly_transport_lost_locked(t);actual_at=meter_at=wallbox_at=last_sent_current_at=t;phase_step_locked(t);}
    assert(phase_state==PHASE_FAULT&&!pulses);
    fixture(3);assert(!phase_shelly_receive_locked(NULL,false,2000));step(3000,0,0,1,true);
    assert(phase_state==PHASE_FAULT&&!pulses); /* Fresh contradiction still latches. */
    fixture(3);assert(!phase_shelly_receive_locked(NULL,false,2000));step(3000,0,0,3,true);
    assert(phase_state==PHASE_FAULT&&!pulses); /* Voltage/output contradiction cannot recover. */
    fixture(3);phase_shelly_ack_locked(false);assert(phase_shelly_uncertain&&phase_state==PHASE_FAULT);
    test_transport_missing();
    test_socket_diagnostics();
    puts("PASS: ESC226 absolute commands, no auxiliary inputs, current-zero hold, voltage freshness, output consistency, lost links and no automatic resend");
}
