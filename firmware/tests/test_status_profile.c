/* The generated include contains the production serializer. Only platform and
   controller inputs are fixtures; no status fields are recreated by this test. */
#include "ems_core.h"
#include "ems_features.h"
#include "evcc_bridge.h"
#include "hardware_profile.h"
#include "shelly_phase.h"
#include "telemetry_core.h"
#include "cJSON.h"
#include <assert.h>
#include <math.h>
#include <stddef.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define VERSION "status-fixture"
#define EMS_BUILD_ID "status-fixture-build"
#define TERMS_REVISION "status-fixture-terms"
#define MALLOC_CAP_8BIT 1
#define ESP_OK 0
#define PHASE_FEEDBACK_GPIO settings.phase_feedback_pin
#define PHASE_FEEDBACK_SECOND_GPIO settings.phase_feedback_second_pin
typedef enum { WIFI_MODE_NULL, WIFI_MODE_STA, WIFI_MODE_AP, WIFI_MODE_APSTA } wifi_mode_t;
typedef struct { int8_t rssi; } wifi_ap_record_t;
typedef struct { uint8_t app_elf_sha256[32]; } esp_app_desc_t;
enum { PHASE_READY, PHASE_STOPPING, PHASE_MOVING, PHASE_SETTLING, PHASE_FAULT };

static settings_t settings,saved_settings;
static energy_t energy;
static double energy_reset_wh[EMS_SOURCES];
static uint32_t energy_reset_date;
static charge_plan_t charge_plan;
static plan_phase_t plan_phase;
static evcc_control_t evcc_control;
static evcc_shell_t evcc_shell;
static int64_t evcc_shell_at;
static pv_control_t pv_control;
static charge_guard_t charge_guard;
static current_calibration_t current_calibration;
static battery_buffer_t battery_buffer;
static expert_values_t expert_saved;
static telemetry_config_t telemetry_config;
static bool telemetry_last_ok;
static int64_t telemetry_last_at;
static bool terms_accepted,evcc_configuring,phase_test_active,boot_validation_pending,ota_in_progress,restarting,backup_in_progress;
static bool fault_storage_ok,phase_mqtt_allowed,phase_mqtt_known,phase_idle_inhibit,phase_gpio_ready;
static bool relay_on[1],phase_shelly_config_ok,phase_startup,phase_link;
static unsigned phase_state,phase_test_goal,phase_mqtt_assumed,phase_candidate;
static int64_t phase_shelly_at,phase_zero_at,phase_stop_at,phase_move_at,phase_hold_until,phase_candidate_at;
static shelly_phase_feedback_t phase_shelly_feedback_value;
static bool battery_use,battery_start_use,control_storage_ok,wallbox_ready,meter_ready,mqtt_online,wifi_online;
static bool reboot_required,expert_reboot_pending,boot_validation_ok,boot_validation_failed,runtime_watchdog_failed,boot_rollback_armed;
static uint32_t runtime_watchdog_tasks;
static bool stats_storage_ok,config_storage_ok,storage_error,plan_storage_ok,wifi_ap_forced,wifi_fallback;
static struct { bool stable; } mode_contact,evu_contact;
static float meter_a[3],actual_a[3],house_a[3],last_sent_a[3],house_power_w,wallbox_w;
static float battery_soc,car_soc,pv_generation_w,battery_charge_w,battery_discharge_w,shell_grid_a,shell_max_a;
static int64_t meter_at,power_at,actual_at,house_power_at,battery_soc_at,car_soc_at,pv_generation_at;
static int64_t battery_charge_at,battery_discharge_at,wallbox_at,house_current_at,last_saved_ms,last_sent_current_at,shell_limits_at;
static int64_t huawei_at;
static int shell_limits_code;
static char huawei_model[32],shell_active_host[64],station_ip[16],csrf_token[33];
static const char *SETUP_SSID="Fixture setup",*DEVICE_HOSTNAME="fixture";
static uint32_t meter_failures,wallbox_requests,mqtt_rejected,wallbox_rx_bytes,wallbox_valid_frames,wallbox_other_address,wallbox_crc_resyncs,wallbox_tx_errors;
static uint8_t wallbox_last_address,wallbox_last_function,wallbox_rx_tail[64];
static uint16_t wallbox_last_register,wallbox_last_count;
static size_t wallbox_rx_next,wallbox_rx_count;
static int64_t wallbox_rx_at;
static int locks;
static unsigned lock_entries,heap_queries,app_description_queries,wifi_queries;
static int64_t clock_ms;
static bool clock_valid,signal_valid;
static wifi_mode_t fixture_wifi_mode;

#define LOCK() do { assert(locks++==0);lock_entries++; } while(0)
#define UNLOCK() do { assert(--locks==0); } while(0)
static int64_t now_ms(void){return clock_ms;}
static uint32_t calendar(int64_t *epoch,int64_t *midnight){*epoch=1792742400;*midnight=1792713600;return clock_valid?20261023:0;}
static int esp_wifi_get_mode(wifi_mode_t *mode){wifi_queries++;*mode=fixture_wifi_mode;return ESP_OK;}
static int esp_wifi_sta_get_ap_info(wifi_ap_record_t *record){wifi_queries++;record->rssi=-57;return signal_valid?ESP_OK:-1;}
static const esp_app_desc_t *esp_app_get_description(void){static const esp_app_desc_t value={{1,2,3,4}};app_description_queries++;return &value;}
static size_t heap_caps_get_minimum_free_size(int flags){assert(flags==MALLOC_CAP_8BIT);heap_queries++;return 42000;}
static size_t heap_caps_get_largest_free_block(int flags){assert(flags==MALLOC_CAP_8BIT);heap_queries++;return 32000;}
static int esp_reset_reason(void){return 1;}
static int gpio_get_level(int pin){return pin==settings.phase_feedback_pin?0:1;}
static bool sdm_meter(void){return sdm_profile(settings.wallbox_meter_type)!=NULL;}
static bool network_wallbox_meter(void){return !strcmp(settings.wallbox_meter_type,"shelly_gen2");}
static int64_t wallbox_meter_ttl(void){return network_wallbox_meter()?EMS_NETWORK_METER_TTL:EMS_METER_TTL;}
static int64_t house_meter_ttl(void){return EMS_HOUSE_TTL;}
static bool feedback_ready(int64_t now){return meter_ready&&fresh(now,actual_at,wallbox_meter_ttl());}
static float target_locked(int64_t now){(void)now;return settings.enabled?settings.manual_current_a:0;}
static bool house_power_capable(void){return strcmp(settings.house_meter_type,"xemex")!=0;}
static bool house_currents_ready_locked(int64_t now){return house_phase_currents_ready(true,house_a,now,house_current_at);}
static void grid_guard_report_locked(int64_t now,float reported[3]){grid_guard_report(&settings,house_currents_ready_locked(now),house_a,reported);}
static battery_power_t battery_power_locked(int64_t now){return battery_power_normalize(battery_charge_w,battery_discharge_w,battery_charge_at,battery_discharge_at,now,EMS_INPUT_TTL);}
static bool battery_ready_locked(int64_t now){return fresh(now,battery_soc_at,EMS_INPUT_TTL)&&battery_power_locked(now).fresh;}
static float active_min_a(void){return settings.min_charge_a;}
static const char *block_reason_locked(int64_t now){return feedback_ready(now)?charge_guard.latched?"charge_stopped":"none":"meter";}
static bool phase_shelly_remote(void){return settings.phase_actuator_type==1||settings.phase_actuator_type==3;}
static bool phase_mqtt_remote(void){return settings.phase_actuator_type==2;}
static bool phase_output_link_locked(void){return phase_link;}
static bool phase_config_quiescent_locked(void){return phase_state==PHASE_READY||phase_state==PHASE_FAULT;}
static bool phase_mqtt_supported_locked(void){return phase_switch_meter_supported(&saved_settings);}
static bool phase_mqtt_stopped_locked(void){return !settings.enabled;}
static bool phase_test_ready_locked(int64_t now){return feedback_ready(now)&&!settings.enabled;}
static const char *phase_test_block_locked(int64_t now){return phase_test_ready_locked(now)?"":"fixture blocked";}
static unsigned phase_shelly_observed_locked(int64_t now){return fresh(now,phase_shelly_at,SHELLY_PHASE_TTL_MS)&&phase_shelly_config_ok?3:0;}
static unsigned phase_observed(void){return 3;}
static bool phase_can_unlock_locked(int64_t now){return feedback_ready(now)&&!settings.enabled;}
#include "phase_status.inc"
#include "status_json_generated.inc"

/* Match real cJSON allocation/deallocation paths, including output buffering.
   Requested bytes and calls inside LOCK measure work, not ESP32 elapsed time. */
typedef union { max_align_t alignment; size_t bytes; } allocation_t;
static size_t used,peak,calls,requested,lock_calls,lock_bytes,fail_at;
static void *tracked_alloc(size_t size){
    if(++calls==fail_at)return NULL;
    allocation_t *p=malloc(sizeof(*p)+size);assert(p);p->bytes=size;
    requested+=size;if(locks){lock_calls++;lock_bytes+=size;}
    used+=size;if(used>peak)peak=used;return p+1;
}
static void tracked_free(void *ptr){if(ptr){allocation_t *p=(allocation_t *)ptr-1;assert(used>=p->bytes);used-=p->bytes;free(p);}}
typedef struct { char *json;size_t bytes,fields,nodes,build_calls,build_bytes,tree_bytes,peak_bytes,locked_calls,locked_bytes;unsigned entries,heap_reads,app_reads,wifi_reads; } measured_t;
static size_t nodes(const cJSON *o){size_t count=1;for(const cJSON *p=o->child;p;p=p->next)count+=nodes(p);return count;}
static measured_t measure(bool token,bool diagnostics,bool legacy){
    assert(!used&&!locks);calls=requested=peak=lock_calls=lock_bytes=0;lock_entries=heap_queries=app_description_queries=wifi_queries=0;
    cJSON *o=legacy?status_json(token):status_json_profile(token,diagnostics);assert(o&&!locks);
    measured_t m={.fields=(size_t)cJSON_GetArraySize(o),.nodes=nodes(o),.build_calls=calls,.build_bytes=requested,.tree_bytes=used,.locked_calls=lock_calls,.locked_bytes=lock_bytes,.entries=lock_entries,.heap_reads=heap_queries,.app_reads=app_description_queries,.wifi_reads=wifi_queries};
    char *serialized=cJSON_PrintUnformatted(o);assert(serialized);m.bytes=strlen(serialized);m.peak_bytes=peak;
    m.json=malloc(m.bytes+1);assert(m.json);memcpy(m.json,serialized,m.bytes+1);
    cJSON_free(serialized);cJSON_Delete(o);assert(!used&&!locks);return m;
}
static void fixture(unsigned scenario){
    settings_defaults(&settings);expert_defaults(&expert_saved);assert(expert_activate(&expert_saved));
    clock_ms=120000;clock_valid=signal_valid=true;fixture_wifi_mode=WIFI_MODE_STA;
    settings.enabled=true;settings.mode=MODE_MANUAL;settings.manual_current_a=10;settings.vehicle_soc_enabled=settings.pv_display_enabled=true;
    settings.battery_protect=true;settings.grid_guard_enabled=true;settings.shell_limits_auto=true;
    strcpy(settings.house_meter_type,"sdm630");strcpy(settings.mqtt_prefix,"fixture/status");
    meter_at=power_at=actual_at=house_power_at=battery_soc_at=car_soc_at=pv_generation_at=clock_ms-1000;
    battery_charge_at=battery_discharge_at=wallbox_at=house_current_at=last_sent_current_at=shell_limits_at=huawei_at=clock_ms-1000;
    for(unsigned i=0;i<3;i++){meter_a[i]=actual_a[i]=last_sent_a[i]=10+i*.125f;house_a[i]=20+i;}
    wallbox_w=6900;house_power_w=-3500;battery_soc=76.5f;car_soc=42;pv_generation_w=10500;battery_charge_w=750;battery_discharge_w=0;
    shell_grid_a=25;shell_max_a=16;shell_limits_code=200;strcpy(shell_active_host,"fixture.local");strcpy(huawei_model,"fixture inverter");
    terms_accepted=fault_storage_ok=control_storage_ok=wallbox_ready=meter_ready=mqtt_online=wifi_online=true;
    stats_storage_ok=config_storage_ok=plan_storage_ok=boot_validation_ok=true;
    evcc_configuring=phase_test_active=boot_validation_pending=ota_in_progress=restarting=backup_in_progress=false;
    reboot_required=expert_reboot_pending=boot_validation_failed=runtime_watchdog_failed=boot_rollback_armed=storage_error=false;
    wifi_ap_forced=wifi_fallback=false;runtime_watchdog_tasks=31;phase_mqtt_allowed=phase_mqtt_known=false;
    phase_state=PHASE_READY;phase_gpio_ready=phase_link=true;phase_startup=phase_idle_inhibit=false;phase_test_goal=0;phase_mqtt_assumed=3;
    phase_candidate=1;phase_zero_at=phase_stop_at=phase_move_at=phase_hold_until=phase_candidate_at=0;
    phase_shelly_config_ok=false;phase_shelly_at=0;memset(&phase_shelly_feedback_value,0,sizeof(phase_shelly_feedback_value));
    relay_on[0]=false;mode_contact.stable=evu_contact.stable=false;
    battery_use=battery_start_use=true;memset(&battery_buffer,0,sizeof(battery_buffer));
    memset(&charge_guard,0,sizeof(charge_guard));memset(&pv_control,0,sizeof(pv_control));memset(&current_calibration,0,sizeof(current_calibration));
    memset(&evcc_control,0,sizeof(evcc_control));evcc_shell=(evcc_shell_t){.state='C',.evcc_usable=true};strcpy(evcc_shell.raw,"C");strcpy(evcc_shell.reason,"ready");evcc_shell_at=clock_ms-500;
    charge_plan=(charge_plan_t){.active=true,.target_wh=18000,.delivered_wh=1250,.deadline_s=1792790000};plan_phase=PLAN_PV;
    memset(&energy,0,sizeof(energy));energy_reset_date=20261001;energy_reset_wh[0]=1000;energy_reset_wh[1]=900;
    for(unsigned i=0;i<EMS_SOURCES;i++){energy.store.total_wh[i]=210000+i*250;energy.boot_wh[i]=3000+i*250;energy.store.undated_wh[i]=500;energy.store.days[0].wh[i]=2700;energy.store.days[0].covered_ms[i]=7200000;}
    energy.store.days[0].date=20261023;energy.store.days[0].charging_ms=3600000;
    telemetry_config=(telemetry_config_t){.schema=1};telemetry_last_ok=true;telemetry_last_at=clock_ms-30000;
    strcpy(telemetry_config.revision,TELEMETRY_REVISION);last_saved_ms=clock_ms-30000;
    meter_failures=3;wallbox_requests=345;wallbox_rx_bytes=16789;wallbox_valid_frames=343;wallbox_other_address=2;wallbox_crc_resyncs=7;wallbox_tx_errors=1;mqtt_rejected=4;
    wallbox_last_address=1;wallbox_last_function=3;wallbox_last_register=100;wallbox_last_count=6;
    wallbox_rx_count=sizeof(wallbox_rx_tail);wallbox_rx_next=13;wallbox_rx_at=clock_ms-25;
    for(unsigned i=0;i<sizeof(wallbox_rx_tail);i++)wallbox_rx_tail[i]=(uint8_t)(i*7);
    strcpy(station_ip,"192.0.2.1");strcpy(csrf_token,"fixture-token-never-from-device");
    if(scenario==1){
        meter_at=power_at=actual_at=house_power_at=battery_soc_at=car_soc_at=pv_generation_at=0;
        battery_charge_at=battery_discharge_at=house_current_at=wallbox_at=last_sent_current_at=shell_limits_at=evcc_shell_at=0;
        meter_ready=signal_valid=clock_valid=false;wallbox_valid_frames=wallbox_rx_count=0;
    }else if(scenario==2){
        settings.enabled=false;settings.phase_switch_enabled=true;settings.phase_actuator_type=2;
        settings.phase_mqtt_output_enabled=true;settings.mqtt_enabled=true;settings.xemex_coils=2;settings.basic_mode=false;
        phase_mqtt_allowed=phase_mqtt_known=true;phase_mqtt_assumed=1;phase_state=PHASE_STOPPING;phase_zero_at=clock_ms-2000;phase_stop_at=clock_ms-5000;
    }else if(scenario==3){
        settings.phase_switch_enabled=true;settings.phase_actuator_type=1;settings.phase_feedback_enabled=true;settings.phase_feedback_count=2;settings.relay_board_enabled=true;
        phase_shelly_config_ok=true;phase_shelly_at=clock_ms-100;phase_shelly_feedback_value.no_closed=true;phase_shelly_feedback_value.output=true;
        phase_state=PHASE_MOVING;phase_move_at=clock_ms-1000;evcc_control.configured=true;
    }else if(scenario==4){
        settings.enabled=false;settings.phase_switch_enabled=true;phase_state=PHASE_FAULT;charge_guard.latched=true;charge_guard.stop_count=5;charge_guard.retry_until=clock_ms+25000;
        battery_buffer.started=battery_buffer.active=true;battery_buffer.started_at=clock_ms-10000;settings.external_mode_input_enabled=settings.evu_input_enabled=true;
        mode_contact.stable=evu_contact.stable=true;fixture_wifi_mode=WIFI_MODE_APSTA;
    }
    saved_settings=settings;
}
static void compare(const measured_t *full,const measured_t *compact,bool token){
    cJSON *a=cJSON_Parse(full->json),*b=cJSON_Parse(compact->json);assert(a&&b);
    unsigned missing=0;
    for(cJSON *p=a->child;p;p=p->next){cJSON *q=cJSON_GetObjectItemCaseSensitive(b,p->string);if(q)assert(cJSON_Compare(p,q,true));else missing++;}
    for(cJSON *p=b->child;p;p=p->next){
        if(!strcmp(p->string,"status_profile")){assert(cJSON_IsString(p)&&!strcmp(p->valuestring,"dashboard"));continue;}
        assert(cJSON_GetObjectItemCaseSensitive(a,p->string));
    }
    assert(!cJSON_GetObjectItemCaseSensitive(a,"status_profile"));
    assert(cJSON_IsString(cJSON_GetObjectItemCaseSensitive(b,"status_profile")));
    static const char *omitted[]={"min_free_heap","largest_free_block","reset_reason","meter_failures","wallbox_requests","wallbox_rx_bytes","wallbox_valid_frames","wallbox_other_address","wallbox_crc_resyncs","wallbox_last_address","wallbox_last_function","wallbox_last_register","wallbox_last_count","wallbox_reply_age_s","wallbox_tx_errors","wallbox_rx_tail_hex","wallbox_rx_age_ms","mqtt_rejected","saved_ago_s","last_sent_a"};
    for(unsigned i=0;i<sizeof(omitted)/sizeof(*omitted);i++){assert(cJSON_GetObjectItemCaseSensitive(a,omitted[i]));assert(!cJSON_GetObjectItemCaseSensitive(b,omitted[i]));}
    assert(missing>=sizeof(omitted)/sizeof(*omitted));
    assert((cJSON_GetObjectItemCaseSensitive(a,"token")!=NULL)==token);
    assert((cJSON_GetObjectItemCaseSensitive(b,"token")!=NULL)==token);
    if(token)assert(!strcmp(cJSON_GetObjectItemCaseSensitive(b,"token")->valuestring,csrf_token));
    assert(cJSON_IsBool(cJSON_GetObjectItemCaseSensitive(b,"enabled")));
    assert(cJSON_IsArray(cJSON_GetObjectItemCaseSensitive(b,"actual_a")));
    assert(cJSON_IsString(cJSON_GetObjectItemCaseSensitive(b,"block_reason")));
    assert(cJSON_IsString(cJSON_GetObjectItemCaseSensitive(b,"build_id")));
    cJSON_Delete(a);cJSON_Delete(b);assert(!used);
}
static uint64_t full_signature(const char *json){
    uint64_t value=UINT64_C(14695981039346656037);
    for(const unsigned char *p=(const unsigned char *)json;*p;p++){value^=*p;value*=UINT64_C(1099511628211);}
    return value;
}
static uint64_t original_fields_signature(const char *json){
    cJSON *o=cJSON_Parse(json);assert(o);
    const char *added[]={"heatpump_enabled","heatpump_ok","heatpump_w","heatpump_age_s","heatpump_source","heatpump_publish_enabled","heatpump_publish_ok","heatpump_mqtt_topic"};
    for(unsigned i=0;i<sizeof(added)/sizeof(*added);i++){
        assert(cJSON_GetObjectItemCaseSensitive(o,added[i]));cJSON_DeleteItemFromObjectCaseSensitive(o,added[i]);
    }
    char *text=cJSON_PrintUnformatted(o);assert(text);uint64_t signature=full_signature(text);
    cJSON_Delete(o);cJSON_free(text);return signature;
}
int main(void){
    cJSON_Hooks hooks={tracked_alloc,tracked_free};cJSON_InitHooks(&hooks);
    const char *names[]={"live","stale","mqtt-phase","shelly-phase","fault"};
    /* Captured from the original full serializer before adding profiles, with
       these exact synthetic fixtures and the standard host board. A full
       response must preserve every field, value and serialized byte. */
    static const uint64_t baseline[5][2]={
        {UINT64_C(14616888087941906349),UINT64_C(14734675399006853868)},
        {UINT64_C(7735195859586356258),UINT64_C(3095951567230116959)},
        {UINT64_C(7339935403127283636),UINT64_C(2441263922852272861)},
        {UINT64_C(13303335152094130585),UINT64_C(1072198529094106360)},
        {UINT64_C(2337048426797253988),UINT64_C(5770360672153892269)}
    };
    puts("scenario,token,profile,json_bytes,root_fields,nodes,build_allocations,build_requested_bytes,tree_bytes,peak_bytes,locked_allocations,locked_requested_bytes");
    for(unsigned scenario=0;scenario<sizeof(names)/sizeof(*names);scenario++)for(unsigned token=0;token<2;token++){
        fixture(scenario);measured_t full=measure(token,true,false),compact=measure(token,false,false),legacy=measure(token,true,true);
        assert(original_fields_signature(full.json)==baseline[scenario][token]);
        assert(!strcmp(full.json,legacy.json));compare(&full,&compact,token);
        assert(compact.bytes<full.bytes&&compact.nodes<full.nodes&&compact.build_calls<full.build_calls);
        assert(compact.tree_bytes<full.tree_bytes&&compact.locked_calls<full.locked_calls&&compact.locked_bytes<full.locked_bytes);
        assert(full.entries==1&&compact.entries==1&&legacy.entries==1);
        assert(full.heap_reads==2&&compact.heap_reads==0);
        assert(full.wifi_reads==2&&compact.wifi_reads==0);
        measured_t *both[]={&full,&compact};
        for(unsigned i=0;i<2;i++){measured_t *m=both[i];printf("%s,%u,%s,%zu,%zu,%zu,%zu,%zu,%zu,%zu,%zu,%zu\n",names[scenario],token,i?"dashboard":"full",m->bytes,m->fields,m->nodes,m->build_calls,m->build_bytes,m->tree_bytes,m->peak_bytes,m->locked_calls,m->locked_bytes);}
        free(full.json);free(compact.json);free(legacy.json);assert(!used&&!locks);
    }
    for(unsigned diagnostics=0;diagnostics<2;diagnostics++){
        fixture(0);measured_t regular=measure(true,diagnostics,false);size_t count=regular.build_calls;free(regular.json);
        for(size_t failure=1;failure<=count;failure++){
            assert(!used&&!locks);calls=requested=peak=lock_calls=lock_bytes=0;fail_at=failure;
            cJSON *o=status_json_profile(true,diagnostics);cJSON_Delete(o);
            if(used)fprintf(stderr,"FAIL: %s allocation %zu retained %zu bytes\n",diagnostics?"full":"dashboard",failure,used);
            assert(!used&&!locks);
        }
        fail_at=0;
        printf("PASS: %s serializer releases all allocations for each of %zu injected allocation failures\n",diagnostics?"full":"dashboard",count);
    }
    cJSON_InitHooks(NULL);
    puts("PASS: actual status serialization; compact/full values; legacy full wrapper; token semantics; conditional phase and stale data; fewer bytes/allocations and less allocation work under LOCK; no retained allocations");
}
