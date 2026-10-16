#include "ems_core.h"
#include "opendtu_mqtt.h"
#include "github_release.h"
#include "esp_flash.h"
#include "mbedtls/sha256.h"
#include "charge_sessions.h"
#include "ems_features.h"
#include "diagnostic_store.h"
#include "firmware_check.h"
#include "hardware_profile.h"
#include "http_encoding.h"
#include "boot_check.h"
#include "ems_build.h"

#include "evcc_bridge.h"
#include "meter_json.h"
#include "shelly_modbus.h"
#include "shelly_phase.h"
#include "em24_modbus.h"
#include "huawei_modbus.h"

#include <math.h>
#include <limits.h>

#include <stdio.h>

#include <stdlib.h>

#include <string.h>

#include <time.h>

#include <sys/time.h>

#include <errno.h>
#include <fcntl.h>

#include "cJSON.h"

#include "driver/uart.h"
#include "driver/gpio.h"

#include "driver/rmt_tx.h"
#include "driver/rmt_encoder.h"

#include "esp_crt_bundle.h"

#include "esp_event.h"

#include "esp_http_server.h"
#include "lwip/sockets.h"
#include "lwip/netdb.h"

#include "lwip/inet.h"

#include "esp_http_client.h"

#include "esp_ota_ops.h"
#include "esp_app_desc.h"

#include "esp_log.h"

#include "esp_mac.h"

#include "esp_netif.h"

#include "esp_netif_sntp.h"

#include "esp_random.h"

#include "esp_system.h"
#include "esp_heap_caps.h"

#include "esp_timer.h"

#include "esp_wifi.h"

#include "freertos/FreeRTOS.h"

#include "freertos/semphr.h"

#include "freertos/task.h"

#include "mqtt_client.h"

#include "nvs.h"

#include "nvs_flash.h"

#define TAG "wallbox_ems"

#define VERSION EMS_VERSION

#define WALLBOX_UART UART_NUM_1

#define XEMEX_UART UART_NUM_2

#define HOUSE_UART UART_NUM_0

#define STATUS_LED_GPIO settings.status_led_pin
#define PHASE_FEEDBACK_GPIO settings.phase_feedback_pin
#define PHASE_FEEDBACK_SECOND_GPIO settings.phase_feedback_second_pin

#define LOCK() xSemaphoreTake(state_lock,portMAX_DELAY)

#define UNLOCK() xSemaphoreGive(state_lock)

static settings_t settings,saved_settings;
static uint32_t wallbox_meter_revision,house_meter_revision;
static evcc_control_t evcc_control;
static evcc_shell_t evcc_shell={.state='?',.reason="pending"};
static char evcc_host[64],evcc_key[33];
static int64_t evcc_shell_at;
static int evcc_shell_http_code;
static uint32_t evcc_poll_generation;
static bool evcc_configuring;
#define TERMS_REVISION "2026-10-09.2"
static bool terms_accepted;
static uint32_t loaded_settings_version;

static energy_t energy;
static double energy_reset_wh[EMS_SOURCES];
static uint32_t energy_reset_date;
static sessions_t sessions;
static ems_events_t event_log;
static diagnostic_store_t fault_log;
static bool fault_dirty,fault_storage_ok=true;
static uint32_t fault_revision;
static int64_t fault_save_attempt;
static charge_plan_t charge_plan;
static plan_phase_t plan_phase;
static bool plan_dirty;
static bool plan_storage_ok=true;
static SemaphoreHandle_t plan_save_lock;
static uint32_t plan_revision;
static int64_t plan_save_attempt;

static pv_control_t pv_control;

static charge_guard_t charge_guard;
static manual_report_filter_t manual_filter;
static current_calibration_t current_calibration;
static bool calibration_dirty;
static int64_t calibration_saved_at;
typedef struct { char host[16],type[20]; float watts; int64_t at; bool locked; } shelly_found_t;
static shelly_found_t shelly_found[12];
static struct {char host[16];shelly_phase_identity_t identity;bool contacts_checked;} phase_shelly_found[12];
static unsigned phase_shelly_found_count;
static bool scan_phase_shelly;
static unsigned shelly_found_count,shelly_scan_progress,shelly_locked_count;
static bool shelly_scan_running,shelly_scan_partial;
static bool scan_tasmota;
static struct {
    char host[64],type[20],path[96];
    float watts;
    int64_t at,started;
    bool running,ok,wallbox;
    int code;
    uint8_t unit_id;
} meter_preview;
static int64_t shelly_scan_started;

static SemaphoreHandle_t state_lock,wifi_action_lock;

static esp_mqtt_client_handle_t mqtt_client;

static bool wallbox_ready,meter_ready,mqtt_online,wifi_online;
static bool shell_scan_running,shell_scan_partial;
static unsigned shell_scan_progress,shell_found_count;
static char shell_found_host[4][64],shell_active_host[64];
static float shell_found_grid[4],shell_found_max[4],shell_grid_a,shell_max_a;
static int64_t shell_limits_at;
static int shell_limits_code;
static bool house_bus_ready,relay_ready,relay_on[1];

static bool config_storage_ok,stats_storage_ok,storage_error,reboot_required,restarting,sntp_ready,factory_reset_requested;
static bool expert_reboot_pending;
static volatile bool boot_validation_pending=true,boot_validation_failed,boot_validation_ok;
static bool boot_config_ok=true,boot_rollback_armed,web_started;
static volatile uint32_t boot_task_seen[5];
/* Permission is persisted; phase commands are deliberately volatile. */
static bool phase_mqtt_allowed,phase_mqtt_saving;
static SemaphoreHandle_t phase_mqtt_send_lock;
static expert_values_t expert_saved;
static bool restart_pending(void){return reboot_required||expert_reboot_pending||boot_validation_pending||boot_validation_failed;}

static float meter_a[3],actual_a[3],xemex_a[3],wallbox_w,house_power_w,house_a[3];
static int64_t house_current_at;

static bool battery_use; /* preferences are persisted separately from charging commands */
static bool battery_start_use; /* allows configured battery support from PV start down to the SOC reserve */
static control_memory_t stored_control_memory;
static bool control_memory_pending,control_storage_ok=true;
static battery_buffer_t battery_buffer;
typedef enum { PHASE_READY, PHASE_STOPPING, PHASE_MOVING, PHASE_SETTLING, PHASE_FAULT } phase_state_t;
static phase_state_t phase_state=PHASE_READY;
static uint8_t phase_goal=3, phase_candidate=3;
static int64_t phase_candidate_at,phase_stop_at,phase_zero_at,phase_move_at,phase_hold_until;
static bool phase_startup=true;
static int64_t phase_attempt_at,phase_no_load_since;
static bool phase_idle_inhibit;
static bool phase_test_active,phase_config_saving;
static uint8_t phase_test_goal;
static int64_t phase_test_until;
static bool phase_gpio_ready;
typedef struct { bool raw,stable; int64_t changed_at; } contact_state_t;
static contact_state_t mode_contact,evu_contact;
static bool contact_gpio_ready;
static float battery_soc;
static int64_t battery_soc_at;
/* Vehicle SOC is informational and never enters the charging controller. */
static float car_soc;
static int64_t car_soc_at;
/* Informational ioBroker values. They do not enter the charging controller. */
static float pv_generation_w,battery_charge_w,battery_discharge_w;
static int64_t pv_generation_at,battery_charge_at,battery_discharge_at;

static float last_sent_a[3];

static int64_t last_sent_current_at;

static int64_t meter_at,actual_at,power_at,pv_at,wallbox_at,house_power_at,last_saved_ms;

static int64_t wifi_lost_at;

static bool wifi_fallback, wifi_scanning, wifi_ap_forced, ota_in_progress, ap_requested;
static bool backup_in_progress;
static SemaphoreHandle_t maintenance_flash_lock;
static opendtu_state_t opendtu;

static bool reset_count_pending;

static int64_t reset_clear_at;

static uint32_t meter_failures,wallbox_requests,mqtt_rejected;

static uint32_t wallbox_rx_bytes,wallbox_valid_frames,wallbox_other_address,wallbox_crc_resyncs;

static uint8_t wallbox_last_address,wallbox_last_function;

static uint16_t wallbox_last_register,wallbox_last_count;

static uint32_t wallbox_tx_errors;

/* Bounded passive trace for diagnosing damaged frames without changing UART settings. */

static uint8_t wallbox_rx_tail[64];

static size_t wallbox_rx_next,wallbox_rx_count;

static int64_t wallbox_rx_at;

static char csrf_token[33],station_ip[16];
static char mqtt_device_id[24]="teenet";

static const char *SETUP_SSID="Wallbox-EMS-Setup";
static const char *DEVICE_HOSTNAME="TeeNet";

static uint8_t reset_press_count;

static int64_t now_ms(void) { return esp_timer_get_time()/1000; }

static TickType_t ticks(uint32_t ms) { return (ms+portTICK_PERIOD_MS-1)/portTICK_PERIOD_MS; }

static void status_led_setup(void) {
    if(STATUS_LED_GPIO<0)return;
    /* Clear the addressable RGB LED once, then release the RMT resources. It
       has no status or diagnostic role after startup. */
    rmt_channel_handle_t channel_handle=NULL; rmt_encoder_handle_t encoder=NULL;
    rmt_tx_channel_config_t channel={.gpio_num=STATUS_LED_GPIO,.clk_src=RMT_CLK_SRC_DEFAULT,
        .resolution_hz=10000000,.mem_block_symbols=64,.trans_queue_depth=1};
    rmt_bytes_encoder_config_t bytes={.bit0={.duration0=4,.level0=1,.duration1=8,.level1=0},
        .bit1={.duration0=8,.level0=1,.duration1=4,.level1=0},.flags.msb_first=1};
    if(rmt_new_tx_channel(&channel,&channel_handle)==ESP_OK &&
       rmt_new_bytes_encoder(&bytes,&encoder)==ESP_OK && rmt_enable(channel_handle)==ESP_OK) {
        uint8_t off[3]={0}; rmt_transmit_config_t cfg={.loop_count=0,.flags.eot_level=0};
        rmt_transmit(channel_handle,encoder,off,sizeof(off),&cfg); rmt_tx_wait_all_done(channel_handle,50);
    }
    if(encoder) rmt_del_encoder(encoder);
    if(channel_handle) { rmt_disable(channel_handle); rmt_del_channel(channel_handle); }
    gpio_config_t output={.pin_bit_mask=1ULL<<STATUS_LED_GPIO,.mode=GPIO_MODE_OUTPUT,
        .pull_up_en=GPIO_PULLUP_DISABLE,.pull_down_en=GPIO_PULLDOWN_ENABLE,.intr_type=GPIO_INTR_DISABLE};
    if(gpio_config(&output)==ESP_OK) gpio_set_level(STATUS_LED_GPIO,0);
}

static uint32_t calendar(int64_t *epoch_ms,int64_t *midnight_ms) {

    struct timeval tv; gettimeofday(&tv,NULL); *epoch_ms=(int64_t)tv.tv_sec*1000+tv.tv_usec/1000;

    if(tv.tv_sec<1704067200 || tv.tv_sec>4102444800LL) { *midnight_ms=0; return 0; }

    struct tm date; localtime_r(&tv.tv_sec,&date);

    uint32_t key=(date.tm_year+1900)*10000+(date.tm_mon+1)*100+date.tm_mday;

    date.tm_hour=date.tm_min=date.tm_sec=0; date.tm_isdst=-1;

    *midnight_ms=(int64_t)mktime(&date)*1000; return key;

}

static void load_settings(void) {

    settings_defaults(&settings); nvs_handle_t nvs;

    if(config_storage_ok && nvs_open("wallbox_ems",NVS_READONLY,&nvs)==ESP_OK) {

        const char *keys[]={"settings_v21","settings_v20","settings_v19","settings_v18","settings_v16","settings_v15","settings_v14","settings_v13","settings_v12","settings_v11","settings_v10","settings_v9","settings_v8","settings_v7","settings_v6","settings_v5","settings_v4","settings_v3","settings_v2","settings_v1"};

        /* The decoder has its own settings copy. Keep the input off the small
           startup stack as the append-only settings layout grows. */
        unsigned char *blob=malloc(sizeof(settings_t));if(!blob)boot_config_ok=false;
        for(unsigned i=0;blob && i<sizeof(keys)/sizeof(keys[0]);i++) {

            size_t len=sizeof(settings_t);

            esp_err_t err=nvs_get_blob(nvs,keys[i],blob,&len);

            if(err==ESP_ERR_NVS_NOT_FOUND) continue;

            if(err==ESP_OK && len>=sizeof(uint32_t))memcpy(&loaded_settings_version,blob,sizeof(uint32_t));
            if(err!=ESP_OK || !settings_decode(blob,len,&settings)){boot_config_ok=false;ESP_LOGW(TAG,"Stored settings rejected; defaults loaded");}

            break;

        }
        free(blob);

        float offset;size_t offset_len=sizeof(offset);
        if(nvs_get_blob(nvs,"auto_offset",&offset,&offset_len)==ESP_OK && offset_len==sizeof(offset) &&
           isfinite(offset) && offset>=0 && offset<=fminf(2,settings.min_charge_a-EMS_MIN_CHARGE_A))
            settings.current_offset_a=offset;

        nvs_close(nvs);

    }

    /* TeeNet supports automatic-direction RS485 modules only. Keep the
       legacy struct members for compatible NVS decoding, but never drive
       DE/RE pins from an old configuration. The power estimate uses cos phi 1. */
    settings.wallbox_rts_pin=-1;
    settings.xemex_rts_pin=-1;
    settings.house_rts_pin=-1;
    settings.power_factor=1;
    settings.wallbox_address=1;
    settings.house_xemex_coils=3;
    settings.reserved_http_meter_enabled=false;
    /* Repeated vehicle tests established 6.0 kW as the reliable three-phase
       floor. Normalize earlier 8.0/8.8 A commissioning values accordingly. */
    if(settings.min_charge_a>=8.0f && settings.min_charge_a<=8.81f)
        settings.min_charge_a=8.7f;
    if(settings.manual_current_a>0 && settings.manual_current_a<settings.min_charge_a)
        settings.manual_current_a=settings.min_charge_a;
    settings.reserved_http_meter_host[0]=0;
    settings.reserved_feedback_source=0;
    if(settings.mqtt_input_source==1)settings.mqtt_input_source=0;
    settings.homeassistant_enabled=settings.mqtt_enabled;
    saved_settings=settings;

}

#include "control_storage.inc"

/* EN/RST also resets RTC data on this board. Persist only a tiny boot counter.

   Power cycles and RST are indistinguishable; normal software reboots do not count. */

static void reset_sequence_start(void) {

    nvs_handle_t nvs; if(!config_storage_ok || nvs_open("recovery",NVS_READWRITE,&nvs)!=ESP_OK) return;

    esp_reset_reason_t reason=esp_reset_reason(); uint8_t previous=0;

    nvs_get_u8(nvs,"rapid_boots",&previous);

    if(reason==ESP_RST_POWERON || reason==ESP_RST_EXT) reset_press_count=previous<5?previous+1:1;

    else reset_press_count=0;

    nvs_set_u8(nvs,"rapid_boots",reset_press_count); nvs_commit(nvs); nvs_close(nvs);

    reset_count_pending=reset_press_count>0;

    if(reset_press_count>=5) factory_reset_requested=true;

}

static void reset_sequence_clear(void) {

    nvs_handle_t nvs;

    if(nvs_open("recovery",NVS_READWRITE,&nvs)!=ESP_OK) return;

    esp_err_t err=nvs_set_u8(nvs,"rapid_boots",0);

    if(err==ESP_OK) err=nvs_commit(nvs);

    nvs_close(nvs);

    if(err==ESP_OK) { reset_press_count=0; reset_count_pending=false; }

}

static esp_err_t save_config(const settings_t *config) {

    if(!config_storage_ok) return ESP_FAIL;

    nvs_handle_t nvs; esp_err_t err=nvs_open("wallbox_ems",NVS_READWRITE,&nvs);

    if(err!=ESP_OK) return err;

    err=nvs_set_blob(nvs,"settings_v21",config,sizeof(*config));

    if(err==ESP_OK) err=nvs_commit(nvs);

    nvs_close(nvs); return err;

}

static void load_energy(void) {

    energy.store.version=2;
    sessions_init(&sessions);

    for(int i=0;i<EMS_HISTORY;i++) energy.history[i].minute=-1;

    nvs_handle_t nvs;

    if(stats_storage_ok && nvs_open_from_partition("stats","energy",NVS_READONLY,&nvs)==ESP_OK) {

        size_t size=0; esp_err_t err=nvs_get_blob(nvs,"totals",NULL,&size);
        if(err==ESP_OK) {
            void *blob=size<=sizeof(energy.store)?malloc(size):NULL;
            if(!blob) err=ESP_ERR_NO_MEM;
            else {
                err=nvs_get_blob(nvs,"totals",blob,&size);
                if(err==ESP_OK && !energy_store_decode(blob,size,&energy.store)) err=ESP_ERR_INVALID_SIZE;
                free(blob);
            }
        }
        if(err!=ESP_ERR_NVS_NOT_FOUND && err!=ESP_OK) {
            memset(&energy.store,0,sizeof(energy.store)); energy.store.version=2;
            storage_error=true; stats_storage_ok=false;
        }

        size=0;
        err=nvs_get_blob(nvs,"sessions",NULL,&size);
        if(err==ESP_OK){
            void *blob=size<=sizeof(session_store_t)?malloc(size):NULL;
            if(!blob)err=ESP_ERR_NO_MEM;
            else{err=nvs_get_blob(nvs,"sessions",blob,&size);if(err==ESP_OK&&!sessions_restore(&sessions,blob,size,settings.price_kwh,settings.solar_price_kwh))err=ESP_ERR_INVALID_SIZE;free(blob);}
        }
        if(err!=ESP_OK&&err!=ESP_ERR_NVS_NOT_FOUND){storage_error=true;stats_storage_ok=false;}

        size=sizeof(energy_reset_wh);
        err=nvs_get_blob(nvs,"reset_base",energy_reset_wh,&size);
        if(err!=ESP_OK||size!=sizeof(energy_reset_wh))memset(energy_reset_wh,0,sizeof(energy_reset_wh));
        if(nvs_get_u32(nvs,"reset_date",&energy_reset_date)!=ESP_OK)energy_reset_date=0;
        for(int i=0;i<EMS_SOURCES;i++)if(!isfinite(energy_reset_wh[i])||energy_reset_wh[i]<0||energy_reset_wh[i]>energy.store.total_wh[i]){
            memset(energy_reset_wh,0,sizeof(energy_reset_wh));energy_reset_date=0;break;
        }
        nvs_close(nvs);

    }

}

static bool save_energy(void) {

    if(!stats_storage_ok) return false;

    energy_store_t *snapshot=malloc(sizeof(*snapshot));
    session_store_t *session_snapshot=malloc(sizeof(*session_snapshot));

    if(!snapshot||!session_snapshot) { free(snapshot);free(session_snapshot);LOCK(); storage_error=true; UNLOCK(); return false; }

    double reset_snapshot[EMS_SOURCES];uint32_t reset_date_snapshot;
    LOCK(); *snapshot=energy.store;*session_snapshot=sessions.store;memcpy(reset_snapshot,energy_reset_wh,sizeof(reset_snapshot));reset_date_snapshot=energy_reset_date; UNLOCK();

    nvs_handle_t nvs; esp_err_t err=nvs_open_from_partition("stats","energy",NVS_READWRITE,&nvs);

    if(err==ESP_OK) {

        err=nvs_set_blob(nvs,"totals",snapshot,sizeof(*snapshot));
        if(err==ESP_OK)err=nvs_set_blob(nvs,"sessions",session_snapshot,sizeof(*session_snapshot));
        if(err==ESP_OK)err=nvs_set_blob(nvs,"reset_base",reset_snapshot,sizeof(reset_snapshot));
        if(err==ESP_OK)err=nvs_set_u32(nvs,"reset_date",reset_date_snapshot);

        if(err==ESP_OK) err=nvs_commit(nvs);

        nvs_close(nvs);

    }

    free(snapshot);free(session_snapshot); LOCK(); storage_error=err!=ESP_OK; if(err==ESP_OK){last_saved_ms=now_ms();sessions.checkpoint=false;} UNLOCK();

    return err==ESP_OK;

}

static bool sdm_meter(void) { return sdm_profile(settings.wallbox_meter_type)!=NULL; }
static bool shelly_meter_type(const char *type) {
    return !strcmp(type,"shelly_gen2") || !strcmp(type,"shelly_em1");
}
static bool network_wallbox_meter(void) {
    return shelly_meter_type(settings.wallbox_meter_type) || !strcmp(settings.wallbox_meter_type,"em24_tcp");
}
static bool network_house_meter(void) {
    return !strcmp(settings.house_meter_type,"tasmota") || !strcmp(settings.house_meter_type,"huawei") || !strcmp(settings.house_meter_type,"em24_tcp") || shelly_meter_type(settings.house_meter_type);
}
/* A missed network poll must not stop charging immediately. Direct RS485
   measurements keep their tighter timeout; network meters get a short grace
   period and still fail closed after a sustained outage. */
static int64_t wallbox_meter_ttl(void) {
    return network_wallbox_meter()?EMS_NETWORK_METER_TTL:EMS_METER_TTL;
}
static int64_t house_meter_ttl(void) {
    return network_house_meter()?EMS_NETWORK_HOUSE_TTL:EMS_HOUSE_TTL;
}
static bool feedback_ready(int64_t now) {
    return meter_ready && (!network_wallbox_meter() || fresh(now,actual_at,wallbox_meter_ttl()));
}
static bool serial_house_meter(void) { return !strcmp(settings.house_meter_type,"xemex") || sdm_profile(settings.house_meter_type)!=NULL; }
static bool house_power_capable(void) { return network_house_meter() || sdm_profile(settings.house_meter_type)!=NULL; }
static bool house_three_phase_current(void) {
    return (!strcmp(settings.house_meter_type,"xemex") && settings.house_xemex_coils==3) ||
        !strcmp(settings.house_meter_type,"sdm630") || !strcmp(settings.house_meter_type,"sdm630mct") ||
        !strcmp(settings.house_meter_type,"shelly_gen2") || !strcmp(settings.house_meter_type,"em24_tcp") ||
        (!strcmp(settings.house_meter_type,"huawei") && settings.huawei_enabled);
}

static bool unsupported_meter(void) {
    return strcmp(settings.wallbox_meter_type,"xemex") && !sdm_meter() && !network_wallbox_meter();
}

static float active_min_a(void) { return settings.min_charge_a; }

    /* Fixed contact assignment: GPIO13 closed to GND confirms three phases;
       optional GPIO14 closed confirms one phase. Two contacts must be opposite. */
#include "phase_mqtt_runtime.inc"
#include "phase_shelly_runtime.inc"
#include "phase_config_guard.inc"
#include "phase_test_runtime.inc"
static uint8_t phase_observed(void) {
    if(phase_shelly_remote())return (uint8_t)phase_shelly_observed_locked(now_ms());
    return settings.phase_feedback_enabled?(uint8_t)phase_feedback_contacts(gpio_get_level(PHASE_FEEDBACK_GPIO)==0,
        settings.phase_feedback_count==2&&gpio_get_level(PHASE_FEEDBACK_SECOND_GPIO)==0,
        settings.phase_feedback_count,settings.phase_feedback_closed_is_single):0;
}

static bool phase_can_unlock_locked(int64_t now) {
    if(phase_mqtt_remote())return phase_gpio_ready&&phase_state==PHASE_FAULT&&phase_mqtt_allowed&&
        phase_output_link_locked()&&phase_mqtt_supported_locked()&&phase_mqtt_stopped_locked();
    if(phase_shelly_remote()){
        unsigned observed=phase_shelly_observed_locked(now);
        return phase_state==PHASE_FAULT && observed &&
            phase_shelly_at>phase_shelly_command_at && now-phase_shelly_command_at>1000 &&
            fresh(now,actual_at,wallbox_meter_ttl()) &&
            isfinite(actual_a[0]) && actual_a[0]>=0 && actual_a[0]<1 &&
            isfinite(actual_a[1]) && actual_a[1]>=0 && actual_a[1]<1 &&
            isfinite(actual_a[2]) && actual_a[2]>=0 && actual_a[2]<1;
    }
    return settings.phase_switch_enabled && phase_fault_reset_allowed(phase_state==PHASE_FAULT,
        phase_gpio_ready,settings.phase_feedback_enabled,phase_gpio_ready?phase_observed():0,
        relay_on[0],fresh(now,actual_at,wallbox_meter_ttl()),actual_a);
}

static void relay_set(unsigned index,bool energized) {
    if(settings.phase_actuator_type!=0 || !relay_ready || !settings.relay_board_enabled || index>0) return;
    gpio_set_level(settings.relay1_pin,
        energized?1:0);
    relay_on[index]=energized;
}

static bool phase_actuator_begin_locked(uint8_t goal,int64_t now){
    if(phase_mqtt_remote())return phase_mqtt_begin_locked(goal,now);
    if(phase_shelly_remote())return phase_shelly_begin_locked(goal,now);
    relay_set(0,goal==1);return true;
}
static void phase_setup(void) {
    settings.charge_phases=settings.phase_switch_enabled?3:settings.fixed_charge_phases; settings.manual_phases=settings.charge_phases;
    if(!settings.relay_board_enabled) return;
    if(phase_mqtt_remote()){phase_mqtt_setup_locked(now_ms());return;}
    if(phase_shelly_remote()){
        relay_ready=phase_gpio_ready=true;
        if(settings.phase_switch_enabled){phase_goal=3;phase_move_at=now_ms();phase_state=PHASE_MOVING;}
        return; /* Never touch GPIO12/13 for the Shelly profile. */
    }
    int off=0;
    gpio_set_level(settings.relay1_pin,off);
    gpio_config_t output={.pin_bit_mask=(1ULL<<settings.relay1_pin),.mode=GPIO_MODE_OUTPUT,
        .pull_up_en=GPIO_PULLUP_DISABLE,
        .pull_down_en=GPIO_PULLDOWN_ENABLE,.intr_type=GPIO_INTR_DISABLE};
    relay_ready=gpio_config(&output)==ESP_OK;
    relay_set(0,false);
    if(!settings.phase_switch_enabled) return;
    if(!settings.phase_feedback_enabled) phase_gpio_ready=relay_ready;
    else {
        gpio_config_t input={.pin_bit_mask=(1ULL<<PHASE_FEEDBACK_GPIO)|
            (settings.phase_feedback_count==2?(1ULL<<PHASE_FEEDBACK_SECOND_GPIO):0),.mode=GPIO_MODE_INPUT,
            .pull_up_en=GPIO_PULLUP_ENABLE,.pull_down_en=GPIO_PULLDOWN_DISABLE,.intr_type=GPIO_INTR_DISABLE};
        phase_gpio_ready=relay_ready && gpio_config(&input)==ESP_OK;
    }
    if(!phase_gpio_ready) phase_state=PHASE_FAULT;
    else {
        /* Let the deenergized contactor return before evaluating position.
           Startup stays blocked through normal feedback and fresh-data checks. */
        phase_goal=3;phase_move_at=now_ms();phase_state=PHASE_MOVING;
    }
}

/* Both external inputs are potential-free contacts to GND. The internal
   pull-up keeps an open or disconnected contact in its inactive state. */
static void contact_setup(void) {
    uint64_t mask=0;
    if(settings.external_mode_input_enabled) mask|=1ULL<<settings.external_mode_input_pin;
    if(settings.evu_input_enabled) mask|=1ULL<<settings.evu_input_pin;
    if(!mask) { contact_gpio_ready=true; return; }
    gpio_config_t input={.pin_bit_mask=mask,.mode=GPIO_MODE_INPUT,
        .pull_up_en=GPIO_PULLUP_ENABLE,.pull_down_en=GPIO_PULLDOWN_DISABLE,.intr_type=GPIO_INTR_DISABLE};
    contact_gpio_ready=gpio_config(&input)==ESP_OK;
    if(contact_gpio_ready) {
        mode_contact.raw=mode_contact.stable=settings.external_mode_input_enabled && gpio_get_level(settings.external_mode_input_pin)==0;
        evu_contact.raw=evu_contact.stable=settings.evu_input_enabled && gpio_get_level(settings.evu_input_pin)==0;
    }
}

static void contact_sample(contact_state_t *contact,bool raw,int64_t now) {
    if(raw!=contact->raw) { contact->raw=raw; contact->changed_at=now; }
    if(contact->stable!=contact->raw && now-contact->changed_at>=ems_param(EP_CONTACT_DEBOUNCE)) contact->stable=contact->raw;
}

static float target_locked(int64_t now);
static void mode_transition_locked(control_mode_t previous,float previous_target,int64_t now);

static void contacts_step_locked(int64_t now) {
    if(!contact_gpio_ready) return;
    if(settings.external_mode_input_enabled && !evcc_control.configured && !evcc_configuring) {
        contact_sample(&mode_contact,gpio_get_level(settings.external_mode_input_pin)==0,now);
        control_mode_t wanted=mode_contact.stable?MODE_PV:MODE_MANUAL;
        if(settings.mode!=wanted) {
            float previous_target=target_locked(now);control_mode_t previous=settings.mode;
            if(wanted==MODE_MANUAL && previous_target>0){settings.manual_phases=settings.charge_phases;settings.manual_current_a=previous_target;}
            settings.mode=wanted;
            mode_transition_locked(previous,previous_target,now);
        }
    }
    if(settings.evu_input_enabled) contact_sample(&evu_contact,gpio_get_level(settings.evu_input_pin)==0,now);
    else evu_contact.stable=false;
}

static battery_power_t battery_power_locked(int64_t now) {
    return battery_power_normalize(battery_charge_w,battery_discharge_w,battery_charge_at,battery_discharge_at,now,EMS_INPUT_TTL);
}
static float allocation_grid_locked(int64_t now,float grid_w) {
    battery_power_t power=battery_power_locked(now);
    return pv_allocation_grid(&settings,actual_a,grid_w,power.charge_w,power.discharge_w,battery_soc,
        power.fresh && fresh(now,battery_soc_at,EMS_INPUT_TTL));
}

static float phase_surplus_w_locked(int64_t now) {
    if(!fresh(now,house_power_at,house_meter_ttl()) || !fresh(now,actual_at,wallbox_meter_ttl())) return NAN;
    float measured=estimated_charge_power(&settings,actual_a);
    if(!isfinite(measured)) return NAN;
    battery_power_t power=battery_power_locked(now);
    float reserve=settings.battery_protect?(power.fresh?power.discharge_w:EMS_BATTERY_DISCHARGE_MAX_W):0;
    float allowance=0;
    if(settings.battery_protect && power.fresh && fresh(now,battery_soc_at,EMS_INPUT_TTL) && battery_soc>settings.battery_reserve_soc) {
        if(battery_start_use)allowance=settings.battery_assist_limit_w;
        else if(battery_buffer.active)allowance=fminf(power.discharge_w,settings.battery_cloud_limit_w);
    }
    return measured-allocation_grid_locked(now,house_power_w)+settings.zero_reserve_w-reserve+allowance;
}


#include "phase_status.inc"
#include "phase_controller.inc"

static bool pv_fresh_locked(int64_t now) {

    return settings.zero_feed_enabled?(fresh(now,house_power_at,house_meter_ttl()) && fresh(now,pv_at,house_meter_ttl())):fresh(now,pv_at,EMS_INPUT_TTL);

}

static bool battery_ready_locked(int64_t now) {

    return fresh(now,battery_soc_at,EMS_INPUT_TTL) && battery_power_locked(now).fresh;

}

static float pv_available_current_locked(int64_t now,float grid_w) {
    grid_w=allocation_grid_locked(now,grid_w);
    battery_power_t power=battery_power_locked(now);
    bool charging=pv_control.target_a>0 && fmaxf(actual_a[0],fmaxf(actual_a[1],actual_a[2]))>=1;
    return battery_support_current(&battery_buffer,&settings,actual_a,grid_w,battery_soc,
        fresh(now,battery_soc_at,EMS_INPUT_TTL),power.discharge_w,power.fresh,
        battery_use,battery_start_use,charging,active_min_a(),now);
}

static bool pv_permitted_locked(int64_t now) {

    return settings.mode==MODE_PV && settings.enabled && !charge_guard.latched &&
        (!settings.phase_switch_enabled || phase_state==PHASE_READY) &&

        !phase_test_active && !restart_pending() && !restarting && !ota_in_progress && !phase_mqtt_saving && !unsupported_meter() &&

        feedback_ready(now) && wallbox_ready && fresh(now,meter_at,wallbox_meter_ttl()) &&

        fresh(now,actual_at,wallbox_meter_ttl()) && fresh(now,wallbox_at,ems_param_ms(EP_SHELL_TTL)) && pv_fresh_locked(now);

}

static bool house_currents_ready_locked(int64_t now) {
    return house_phase_currents_ready(house_three_phase_current(),house_a,now,house_current_at);
}

static float unlatched_target_locked(int64_t now) {
    if(!terms_accepted || evcc_configuring || phase_mqtt_saving)return 0;
    if(evcc_control.configured && (!evcc_control.enabled || evcc_control.inhibited || evcc_control.expired ||
       evcc_control.last_contact<=0 || now<evcc_control.last_contact || now-evcc_control.last_contact>=EVCC_LEASE_MS ||
       !evcc_shell_fresh(&evcc_shell,evcc_shell_at,now) || evcc_shell.state=='F'))return 0;
    if(evcc_control.configured && settings.phase_switch_enabled &&
       (phase_idle_inhibit || evcc_desired_phases(&evcc_control,settings.charge_phases)!=settings.charge_phases))return 0;

    if(phase_config_saving || phase_test_active || restart_pending() || restarting || ota_in_progress || unsupported_meter()) return 0;
    if(settings.phase_switch_enabled && phase_state!=PHASE_READY) return 0;
    if(charge_plan.active && plan_phase==PLAN_CLOCK)return 0;

    if(settings.mode==MODE_PV) {

        if(!pv_permitted_locked(now) || !fresh(now,pv_control.last_at,5000)) {

            return 0; /* Only status_task advances the controller state. */

        }

        float target=pv_control.target_a;
        if(settings.evu_input_enabled && evu_contact.stable)
            target=settings.evu_limit_a>0?fminf(target,settings.evu_limit_a):0;
        return grid_guard_target(&settings,target,house_currents_ready_locked(now));

    }

    float target=control_target(&settings,feedback_ready(now) && wallbox_ready && fresh(now,meter_at,wallbox_meter_ttl()),

        fresh(now,actual_at,wallbox_meter_ttl()),pv_fresh_locked(now));
    if(evcc_control.configured && settings.charge_phases==1)target=fminf(target,3500/settings.nominal_v);
    if(settings.evu_input_enabled && evu_contact.stable)
        target=settings.evu_limit_a>0?fminf(target,settings.evu_limit_a):0;
    return grid_guard_target(&settings,target,house_currents_ready_locked(now));

}

static float target_locked(int64_t now) {

    float requested=unlatched_target_locked(now);

    return charge_guard_blocked(&charge_guard,now)?0:requested;

}

static void mode_transition_locked(control_mode_t previous,float previous_target,int64_t now) {
    if(settings.mode==MODE_PV && previous==MODE_MANUAL && settings.enabled) {
        if(settings.zero_feed_enabled && fresh(now,house_power_at,house_meter_ttl()) && fresh(now,actual_at,wallbox_meter_ttl())) {
            settings.pv_surplus_a=pv_available_current_locked(now,house_power_w);pv_at=now;
        }
        if(pv_control_takeover(&pv_control,pv_permitted_locked(now) && !charge_guard_blocked(&charge_guard,now),
            previous_target,actual_a,settings.charge_phases,active_min_a(),settings.max_charge_a,now))return;
    }
    pv_control_step(&pv_control,false,0,active_min_a(),settings.max_charge_a,now);
}

static const char *block_reason_locked(int64_t now) {

    if(restarting || ota_in_progress || phase_mqtt_saving) return "maintenance";

    if(phase_test_active)return "phase_test";
    if(boot_validation_pending)return "firmware_check";
    if(boot_validation_failed)return "firmware_failed";
    if(restart_pending()) return "reboot";
    if(!terms_accepted)return "agreement";
    if(evcc_configuring)return "maintenance";
    if(evcc_control.configured) {
        if(evcc_control.inhibited)return "evcc_local_stop";
        if(evcc_control.expired)return "evcc_timeout";
        if(!evcc_shell_fresh(&evcc_shell,evcc_shell_at,now))return "evcc_status";
        if(evcc_shell.state=='F')return "evcc_fault";
        if(!evcc_control.enabled)return "evcc_waiting";
    }
    if(settings.phase_switch_enabled && phase_state==PHASE_FAULT) return "phase_fault";
    if(settings.phase_switch_enabled && phase_state!=PHASE_READY) return "phase_switching";
    if(settings.phase_switch_enabled && phase_idle_inhibit && settings.manual_phases==1 && settings.charge_phases==3 && settings.mode==MODE_MANUAL) return "phase_idle";

    if(unsupported_meter()) return "unsupported_meter";

    if(!settings.enabled || settings.mode==MODE_OFF) return "off";
    if(charge_plan.active && plan_phase==PLAN_CLOCK)return "plan_clock";
    if(settings.evu_input_enabled && evu_contact.stable && settings.evu_limit_a<=0) return "evu_stop";

    if(charge_guard.latched) return "charge_ended";
    if(charge_guard_blocked(&charge_guard,now)) return "charge_retry";

    if(settings.mode==MODE_MANUAL && settings.manual_current_a<active_min_a()) return "manual_stop";

    if(settings.mode==MODE_PV && settings.battery_protect && pv_control.target_a<=0 && settings.pv_surplus_a<active_min_a()) {
        if(!fresh(now,battery_soc_at,EMS_INPUT_TTL))return "battery_stale";
        if(!battery_power_locked(now).fresh)return "battery_power_stale";
        if(battery_soc<=settings.battery_reserve_soc)return "battery_reserve";
    }

    if(!wallbox_ready) return "uart";

    if(!feedback_ready(now) || !fresh(now,meter_at,wallbox_meter_ttl())) return "meter";

    if(!fresh(now,actual_at,wallbox_meter_ttl())) return "feedback";

    if(settings.mode==MODE_PV && !pv_fresh_locked(now)) return settings.zero_feed_enabled?"house_meter":"pv_stale";

    if(settings.mode==MODE_PV && battery_start_use && pv_control.target_a<=0 && settings.pv_surplus_a<active_min_a() &&
       battery_buffer.assist_response_at>0 && now-battery_buffer.assist_response_at>=EMS_BATTERY_RESPONSE_MS)
        return "battery_no_response";

    if(settings.mode==MODE_PV) {

        if(!fresh(now,wallbox_at,ems_param_ms(EP_SHELL_TTL))) return "wallbox";

        if(pv_control.phase==PV_COOLDOWN) return "pv_cooldown";

        if(pv_control.phase==PV_STARTING) return "pv_starting";

        if(pv_control.phase==PV_STOPPING) return "pv_stopping";

        if(pv_control.target_a<=0) return "pv_waiting";

    }

    if(target_locked(now)==0) return "below_minimum";
    if(settings.grid_guard_enabled && !house_currents_ready_locked(now)) return "grid_fallback";

    return "none";

}

static void grid_guard_report_locked(int64_t now,float reported[3]) {
    grid_guard_report(&settings,house_currents_ready_locked(now),house_a,reported);
}

#include "diagnostic_storage.inc"

/* Caller holds state_lock. Flash persistence is batched outside this lock. */
static void event_locked(const char *reason) {
    int64_t epoch,midnight,now=now_ms();bool clock_ok=calendar(&epoch,&midnight)!=0;
    ems_event_add(&event_log,reason,clock_ok?epoch/1000:0,now/1000,
        target_locked(now)*settings.charge_phases*settings.nominal_v/1000,
        fresh(now,meter_at,wallbox_meter_ttl())?estimated_charge_power(&settings,meter_a)/1000:NAN,
        fresh(now,house_power_at,house_meter_ttl())?house_power_w/1000:NAN,
        fresh(now,battery_soc_at,EMS_INPUT_TTL)?battery_soc:NAN);
    if(diagnostic_store_add(&fault_log,ems_event_recent(&event_log,0))){fault_dirty=true;fault_revision++;}
}
static bool save_plan(void) {
    if(xSemaphoreTake(plan_save_lock,ticks(2000))!=pdTRUE)return false;
    charge_plan_t snapshot;LOCK();snapshot=charge_plan;uint32_t revision=plan_revision;plan_save_attempt=now_ms();UNLOCK();
    nvs_handle_t handle;if(!config_storage_ok||nvs_open("teenet_plan",NVS_READWRITE,&handle)!=ESP_OK){LOCK();plan_storage_ok=false;UNLOCK();xSemaphoreGive(plan_save_lock);return false;}
    esp_err_t error=nvs_set_blob(handle,"plan",&snapshot,sizeof(snapshot));
    if(error==ESP_OK)error=nvs_commit(handle);
    nvs_close(handle);
    LOCK();plan_storage_ok=error==ESP_OK;if(error==ESP_OK && revision==plan_revision)plan_dirty=false;UNLOCK();
    xSemaphoreGive(plan_save_lock);
    return error==ESP_OK;
}
static void load_plan(void) {
    nvs_handle_t handle;if(!config_storage_ok||nvs_open("teenet_plan",NVS_READONLY,&handle)!=ESP_OK)return;
    size_t size=sizeof(charge_plan);esp_err_t error=nvs_get_blob(handle,"plan",&charge_plan,&size);nvs_close(handle);
    if(error!=ESP_OK||size!=sizeof(charge_plan)||charge_plan.version!=1||!isfinite(charge_plan.target_wh)||
       charge_plan.target_wh<500||charge_plan.target_wh>100000||!isfinite(charge_plan.delivered_wh)||
       charge_plan.delivered_wh<0||charge_plan.delivered_wh>charge_plan.target_wh||
       charge_plan.deadline_s<1704067200||charge_plan.deadline_s>4102444800LL)
        memset(&charge_plan,0,sizeof(charge_plan));
    charge_plan.last_total_wh=energy.store.total_wh[0];
    if(!settings.charge_plan_enabled){charge_plan.active=false;charge_plan.complete=false;charge_plan.expired=false;}
    plan_phase=charge_plan.active?PLAN_PAUSED:charge_plan.complete?PLAN_COMPLETE:charge_plan.expired?PLAN_EXPIRED:PLAN_OFF;
}

static bool init_uart(uart_port_t port,int tx,int rx,int de,int baud,unsigned format) {

    if(de>=0) { /* receive during initialization; DE and /RE share this line */
        gpio_set_level(de,0); gpio_set_direction(de,GPIO_MODE_OUTPUT);
    }
    uart_config_t cfg={.baud_rate=baud,.data_bits=UART_DATA_8_BITS,.parity=format==1?UART_PARITY_EVEN:format==2?UART_PARITY_ODD:UART_PARITY_DISABLE,

        .stop_bits=format==3?UART_STOP_BITS_2:UART_STOP_BITS_1,.flow_ctrl=UART_HW_FLOWCTRL_DISABLE,.source_clk=UART_SCLK_DEFAULT};

    bool installed=false; esp_err_t err=uart_param_config(port,&cfg);

    if(err==ESP_OK) err=uart_set_pin(port,tx,rx,de,UART_PIN_NO_CHANGE);

    if(err==ESP_OK) { err=uart_driver_install(port,1024,512,0,NULL,0); installed=err==ESP_OK; }

    if(err==ESP_OK) err=uart_set_mode(port,de<0?UART_MODE_UART:UART_MODE_RS485_HALF_DUPLEX);

    if(err==ESP_OK) err=uart_set_rx_timeout(port,4);

    if(err!=ESP_OK) {

        if(installed) uart_driver_delete(port);

        ESP_LOGE(TAG,"UART %d: %s",port,esp_err_to_name(err)); return false;

    }

    return true;

}

/* Fixed-length frames avoid a 5 ms delay becoming zero at 100 Hz. */

static bool rs485_tcp(uart_port_t port);
static int rs485_read(uart_port_t port,uint8_t *data,size_t length,int wait_ms);
static bool rs485_flush(uart_port_t port);
static bool rs485_write(uart_port_t port,const uint8_t *data,size_t length,int timeout_ms);

static bool read_exact(uart_port_t port,uint8_t *buffer,size_t count,int64_t deadline) {

    size_t got=0;

    while(got<count && now_ms()<deadline) {

        int n=rs485_read(port,buffer+got,count-got,10);

        if(n<0) return false;

        got+=n;

    }

    return got==count;

}

static bool read_xemex_port(uart_port_t port,uint8_t address,int baud,float out[3]) {
    uint8_t req[8]={address,3,0x50,0x0c,0,6}; append_crc(req,6);
    vTaskDelay(ticks((38500u+baud-1)/baud+1));
    if(!rs485_flush(port) || !rs485_write(port,req,8,100))return false;
    uint8_t response[17];
    return read_exact(port,response,17,now_ms()+300) && decode_meter(response,17,address,out);
}

static bool read_meter(float out[3]) {
    return read_xemex_port(XEMEX_UART,settings.xemex_address,settings.meter_baud,out);
}



static bool read_sdm_registers_port(uart_port_t port,uint8_t address,int baud,uint16_t start,uint16_t count,uint8_t *response,size_t length) {
    uint8_t req[8]={address,4,start>>8,start&255,count>>8,count&255}; append_crc(req,6);
    vTaskDelay(ticks((38500u+baud-1)/baud+1));
    if(!rs485_flush(port) || !rs485_write(port,req,8,100))return false;
    return read_exact(port,response,length,now_ms()+400) && valid_frame(response,length) &&
        response[0]==address && response[1]==4 && response[2]==count*2;
}

static bool read_sdm(float out[3],float *total_w) {

    const sdm_profile_t *profile=sdm_profile(settings.wallbox_meter_type); if(!profile) return false;

    uint8_t currents[17],power[9]; size_t length=5+profile->phases*4;

    if(!read_sdm_registers_port(XEMEX_UART,settings.xemex_address,settings.meter_baud,profile->current_register,profile->phases*2,currents,length)) return false;

    if(!read_sdm_registers_port(XEMEX_UART,settings.xemex_address,settings.meter_baud,profile->power_register,2,power,sizeof(power))) return false;

    return decode_sdm_reading(profile,settings.xemex_address,currents,length,power,sizeof(power),out,total_w);

}

static bool read_wallbox_network(float out[3],float *total_w);

static void meter_task(void *arg) {
    unsigned network_failures=0;
    while(true) {
        boot_task_seen[2]=(uint32_t)now_ms();
        uint32_t delay_ms=1000;
        float values[3],watts=0; bool sdm=sdm_meter(),network=network_wallbox_meter();
        LOCK();uint32_t revision=wallbox_meter_revision;UNLOCK();

        bool ok=!unsupported_meter() && (network?read_wallbox_network(values,&watts):sdm?read_sdm(values,&watts):read_meter(values));

        if(ok && !sdm && !network) ok=reconstruct_currents(values,settings.xemex_coils);

        LOCK();
        if(network && revision!=wallbox_meter_revision){UNLOCK();continue;}
        if(ok) {
            if(settings.phase_switch_enabled && phase_state==PHASE_READY &&
               wallbox_inactive_phase_current(&settings,values)) phase_state=PHASE_FAULT;
            if(sdm && settings.charge_phases==1 && sdm_profile(settings.wallbox_meter_type) && sdm_profile(settings.wallbox_meter_type)->phases==1) watts/=3;

            int64_t now=now_ms();

            memcpy(xemex_a,values,sizeof(values));
            memcpy(meter_a,values,sizeof(values)); meter_at=now;
            memcpy(actual_a,values,sizeof(values)); actual_at=now;
            if(sdm || network) { wallbox_w=watts; power_at=now; }
            network_failures=0;

        } else { meter_failures++;if(network){if(network_failures<10)network_failures++;delay_ms=network_failures>=5?60000:network_failures>=3?15000:5000;} }

        UNLOCK(); vTaskDelay(ticks(delay_ms));

    }

}

static void wallbox_task(void *arg) {

    uint8_t req[8],response[256]; size_t used=0;int64_t last_byte=0;

    while(true) {
        boot_task_seen[1]=(uint32_t)now_ms();

        int n=rs485_read(WALLBOX_UART,req+used,1,20);
        if(n!=1) {
            if(n<0 || !rs485_tcp(WALLBOX_UART) || now_ms()-last_byte>500)used=0;
            if(n<0)vTaskDelay(ticks(20));
            continue;
        }
        last_byte=now_ms();

        LOCK();

        wallbox_rx_bytes++; wallbox_rx_at=now_ms();

        wallbox_rx_tail[wallbox_rx_next]=req[used];

        wallbox_rx_next=(wallbox_rx_next+1)%sizeof(wallbox_rx_tail);

        if(wallbox_rx_count<sizeof(wallbox_rx_tail)) wallbox_rx_count++;

        UNLOCK();

        if(++used<8) continue;

        if(!valid_frame(req,8)) { LOCK(); wallbox_crc_resyncs++; UNLOCK(); memmove(req,req+1,7); used=7; continue; }

        LOCK();

        wallbox_valid_frames++; wallbox_last_address=req[0]; wallbox_last_function=req[1];

        wallbox_last_register=((uint16_t)req[2]<<8)|req[3]; wallbox_last_count=((uint16_t)req[4]<<8)|req[5];

        if(req[0]!=settings.wallbox_address) wallbox_other_address++;

        float currents[3]; bool house_ok=fresh(now_ms(),house_power_at,house_meter_ttl());

        int64_t report_now=now_ms();float request=target_locked(report_now);
        control_report(&settings,actual_a,request,house_power_w,house_ok,currents);
        manual_report_smooth(&manual_filter,&settings,request,report_now,currents);
        grid_guard_report_locked(report_now,currents);

        size_t len=modbus_reply(req,8,response,&settings,currents);

        UNLOCK(); used=0;

        if(len) {

            vTaskDelay(ticks(5)); /* >= 3.5 characters at 9600/8E1; automatic transceiver turnaround. */

            bool sent=rs485_write(WALLBOX_UART,response,len,400);

            LOCK(); if(sent) {

                wallbox_requests++; wallbox_at=now_ms();

                if(req[1]==3 && req[2]==0x50 && req[3]==0x0c && req[4]==0 && req[5]==6 && len==17) {

                    memcpy(last_sent_a,currents,sizeof(last_sent_a)); last_sent_current_at=wallbox_at;
                    if(settings.phase_switch_enabled && settings.charge_phases==1 && phase_state==PHASE_READY && request>=settings.min_charge_a && !phase_attempt_at)
                        phase_attempt_at=wallbox_at;

                }

            } else wallbox_tx_errors++; UNLOCK();

        }

    }

}

static cJSON *number_array(const float *values,int count,bool valid) {

    cJSON *a=cJSON_CreateArray(); if(!a) return NULL;

    for(int i=0;i<count;i++) cJSON_AddItemToArray(a,valid?cJSON_CreateNumber(values[i]):cJSON_CreateNull());

    return a;

}

static void nullable(cJSON *o,const char *key,double value,bool valid) {

    if(valid && isfinite(value)) cJSON_AddNumberToObject(o,key,value); else cJSON_AddNullToObject(o,key);

}

static bool read_house_serial(float currents_out[3],float *watts,bool *power_valid) {
    *power_valid=false;
    if(!house_bus_ready && !rs485_tcp(HOUSE_UART))return false;
    if(!strcmp(settings.house_meter_type,"xemex")) {
        bool ok=read_xemex_port(HOUSE_UART,settings.house_address,settings.house_meter_baud,currents_out);
        return ok && reconstruct_currents(currents_out,settings.house_xemex_coils);
    }
    const sdm_profile_t *profile=sdm_profile(settings.house_meter_type); if(!profile || profile->phases!=3) return false;
    uint8_t current_frame[17],power_frame[9]; size_t length=5+profile->phases*4;
    if(!read_sdm_registers_port(HOUSE_UART,settings.house_address,settings.house_meter_baud,
        profile->current_register,profile->phases*2,current_frame,length)) return false;
    if(!read_sdm_registers_port(HOUSE_UART,settings.house_address,settings.house_meter_baud,
        profile->power_register,2,power_frame,sizeof(power_frame))) return false;
    float measured[3]={0},signed_watts=0;
    if(!decode_sdm(current_frame,length,settings.house_address,measured,profile->phases,0,999) ||
       !decode_sdm(power_frame,sizeof(power_frame),settings.house_address,&signed_watts,1,-333333,333333)) return false;
    memcpy(currents_out,measured,sizeof(measured)); *watts=signed_watts;
    *power_valid=true;
    return true;
}

typedef struct { char *data; size_t capacity,used; bool overflow,framed,connection_close; } http_body_t;
static esp_err_t http_body_event(esp_http_client_event_t *event) {
    http_body_t *body=event->user_data;if(!body)return ESP_OK;
    if(event->event_id==HTTP_EVENT_HEADERS_SENT){body->used=0;body->overflow=body->framed=body->connection_close=false;if(body->capacity)body->data[0]=0;}
    else if(event->event_id==HTTP_EVENT_ON_HEADER && event->header_key && event->header_value){
        if(!strcasecmp(event->header_key,"Content-Length") || !strcasecmp(event->header_key,"Transfer-Encoding"))body->framed=true;
        if(!strcasecmp(event->header_key,"Connection") && !strcasecmp(event->header_value,"close"))body->connection_close=true;
    }
    else if(event->event_id==HTTP_EVENT_ON_DATA && event->data_len>0){
        size_t count=(size_t)event->data_len;
        if(body->used+count>=body->capacity)body->overflow=true;
        else{memcpy(body->data+body->used,event->data,count);body->used+=count;body->data[body->used]=0;}
    }
    return ESP_OK;
}

#include "meter_network.inc"
#include "rs485_transport.inc"

#include "huawei_network.inc"

#include "wallbox_network.inc"

static bool read_house_meter(float currents[3],float *watts,bool *currents_valid) {

    char type[sizeof(settings.house_meter_type)],host[sizeof(settings.house_meter_host)];
    char path[sizeof(settings.house_power_path)];
    uint8_t unit;
    LOCK(); bool online=wifi_online;strcpy(type,settings.house_meter_type);strcpy(host,settings.house_meter_host);
    strcpy(path,settings.house_power_path);unit=settings.house_address;UNLOCK();

    if(!online) return false;

    *currents_valid=false;
    if(!strcmp(type,"em24_tcp")){
        em24_reading_t reading;
        if(!read_em24_modbus(host,unit,&reading,2500))return false;
        memcpy(currents,reading.current_a,sizeof(reading.current_a));
        *watts=reading.active_power_w;*currents_valid=true;
        return true;
    }
    if(shelly_meter_type(type)){
        shelly_modbus_reading_t reading;
        if(!read_shelly_modbus(host,type,&reading,2500))return false;
        *watts=reading.active_power_w;
        if(reading.phases==3){memcpy(currents,reading.current_a,sizeof(reading.current_a));*currents_valid=true;}
        return true;
    }

    char url[256];

    if(!house_query_url(type,host,path,url,sizeof(url))) return false;
    char *body=malloc(8192);if(!body)return false;
    int code=http_get_body(url,body,8192,3500);
    bool ok=code==200&&house_power_parse(body,type,path,watts);
    free(body);return ok;

}

static void house_meter_task(void *arg) {
    unsigned network_failures=0;
    while(true) {
        boot_task_seen[3]=(uint32_t)now_ms();
        uint32_t delay_ms=2000;
        if(serial_house_meter()) {
            network_failures=0;
            float watts=0,values[3]; bool power_valid=false;
            if(read_house_serial(values,&watts,&power_valid)) {
                int64_t now=now_ms(); LOCK(); memcpy(house_a,values,sizeof(values)); house_current_at=now;
                if(power_valid) {
                    house_power_w=watts; house_power_at=now;
                    if(settings.zero_feed_enabled && fresh(now,actual_at,wallbox_meter_ttl())) { settings.pv_surplus_a=pv_available_current_locked(now,watts); pv_at=now; }
                }
                UNLOCK();
            }
        } else if(network_house_meter() && strcmp(settings.house_meter_type,"huawei") && (settings.zero_feed_enabled || settings.grid_guard_enabled)) {
            float watts,values[3]={0};bool current_valid=false;
            LOCK();uint32_t revision=house_meter_revision;UNLOCK();
            if(read_house_meter(values,&watts,&current_valid)) {
                network_failures=0;
                int64_t now=now_ms(); LOCK();
                if(revision!=house_meter_revision){UNLOCK();continue;}
                house_power_w=watts; house_power_at=now;
                if(current_valid){memcpy(house_a,values,sizeof(values));house_current_at=now;}
                if(settings.zero_feed_enabled && fresh(now,actual_at,wallbox_meter_ttl())) { settings.pv_surplus_a=pv_available_current_locked(now,watts); pv_at=now; }
                UNLOCK();
            } else {
                if(network_failures<10)network_failures++;
                delay_ms=network_failures>=5?60000:network_failures>=3?15000:5000;
            }
        }

        vTaskDelay(ticks(delay_ms));

    }

}

static cJSON *status_json(bool include_token) {

    cJSON *o=cJSON_CreateObject(); if(!o) return NULL;

    int64_t epoch,midnight,now=now_ms(); uint32_t date=calendar(&epoch,&midnight);

    wifi_mode_t wifi_mode=WIFI_MODE_NULL; esp_wifi_get_mode(&wifi_mode);
    wifi_ap_record_t access_point={0}; bool wifi_signal_ok=esp_wifi_sta_get_ap_info(&access_point)==ESP_OK;

    LOCK();

    bool meter_ok=feedback_ready(now) && fresh(now,meter_at,wallbox_meter_ttl()),power_ok=fresh(now,power_at,(sdm_meter()||network_wallbox_meter())?wallbox_meter_ttl():EMS_INPUT_TTL),actual_ok=feedback_ready(now) && fresh(now,actual_at,wallbox_meter_ttl());

    float reported[3]; bool house_current_ok=fresh(now,house_power_at,house_meter_ttl());

    control_report(&settings,actual_a,target_locked(now),house_power_w,house_current_ok,reported);
    grid_guard_report_locked(now,reported);

    cJSON_AddBoolToObject(o,"terms_accepted",terms_accepted);
    cJSON_AddStringToObject(o,"terms_revision",TERMS_REVISION);
    cJSON_AddBoolToObject(o,"evcc_test_enabled",evcc_control.configured);
    cJSON_AddBoolToObject(o,"evcc_local_stop",evcc_control.inhibited);
    cJSON_AddBoolToObject(o,"evcc_lease_expired",evcc_control.expired);
    cJSON_AddBoolToObject(o,"evcc_status_known",evcc_shell_fresh(&evcc_shell,evcc_shell_at,now));
    cJSON_AddStringToObject(o,"evcc_statuscar",evcc_shell.raw);
    cJSON_AddStringToObject(o,"release_channel","stable");
    cJSON_AddStringToObject(o,"version",VERSION); cJSON_AddStringToObject(o,"build_id",EMS_BUILD_ID);
    cJSON_AddNumberToObject(o,"rs485_modules",2);
    cJSON_AddStringToObject(o,"hardware",EMS_HARDWARE_NAME);
    cJSON_AddStringToObject(o,"hardware_id",EMS_HARDWARE_ID);
    cJSON_AddNumberToObject(o,"default_shell_tx_pin",EMS_DEFAULT_SHELL_TX_PIN);
    cJSON_AddNumberToObject(o,"default_shell_rx_pin",EMS_DEFAULT_SHELL_RX_PIN);
    cJSON_AddNumberToObject(o,"default_meter_tx_pin",EMS_DEFAULT_METER_TX_PIN);
    cJSON_AddNumberToObject(o,"default_meter_rx_pin",EMS_DEFAULT_METER_RX_PIN);
    cJSON_AddNumberToObject(o,"external_mode_input_pin",settings.external_mode_input_pin);
    cJSON *gpio=cJSON_AddObjectToObject(o,"gpio_pins");
    for(unsigned i=0;i<EMS_GPIO_ROLES;i++)cJSON_AddNumberToObject(gpio,gpio_roles[i].id,settings_gpio_value(&settings,i));
    cJSON_AddBoolToObject(o,"gpio_setup_allowed",!phase_test_active&&!boot_validation_pending&&!ota_in_progress&&!restarting&&!backup_in_progress&&!evcc_configuring&&
        hardware_config_change_allowed(settings.enabled||(evcc_control.configured&&evcc_control.enabled),
            phase_config_quiescent_locked(),actual_at>0||meter_at>0,
            fresh(now,actual_at,wallbox_meter_ttl()),actual_a));
    const esp_app_desc_t *app_description=esp_app_get_description();char app_hash[65];
    for(unsigned i=0;i<32;i++)snprintf(app_hash+i*2,3,"%02x",app_description->app_elf_sha256[i]);
    cJSON_AddStringToObject(o,"app_elf_sha256",app_hash);
    cJSON_AddNumberToObject(o,"ota_max_bytes",EMS_OTA_PARTITION_BYTES);
    cJSON_AddBoolToObject(o,"diagnostic_storage_ok",fault_storage_ok);
    cJSON *ages=cJSON_AddObjectToObject(o,"measurement_age_s");
    nullable(ages,"wallbox",actual_at?(now-actual_at)/1000:0,actual_at>0);
    nullable(ages,"house",house_power_at?(now-house_power_at)/1000:0,house_power_at>0);
    nullable(ages,"battery",battery_soc_at?(now-battery_soc_at)/1000:0,battery_soc_at>0);
    nullable(ages,"pv",pv_generation_at?(now-pv_generation_at)/1000:0,pv_generation_at>0);
    cJSON_AddStringToObject(o,"mode",mode_name(settings.mode));
    cJSON_AddBoolToObject(o,"charge_plan_enabled",settings.charge_plan_enabled);
    cJSON_AddBoolToObject(o,"huawei_enabled",settings.huawei_enabled);
    cJSON_AddBoolToObject(o,"huawei_ok",fresh(now,huawei_at,15000));
    cJSON_AddStringToObject(o,"huawei_model",huawei_model);
    cJSON_AddStringToObject(o,"battery_source",settings.huawei_enabled && settings.huawei_battery?"Huawei":"MQTT");
    cJSON_AddStringToObject(o,"pv_source",settings.huawei_enabled && settings.huawei_pv?"Huawei":"MQTT");
    cJSON *plan=cJSON_AddObjectToObject(o,"charge_plan");
    cJSON_AddBoolToObject(plan,"active",charge_plan.active);
    cJSON_AddStringToObject(plan,"phase",charge_plan_phase_name(plan_phase));
    cJSON_AddNumberToObject(plan,"target_kwh",charge_plan.target_wh/1000);
    cJSON_AddNumberToObject(plan,"delivered_kwh",charge_plan.delivered_wh/1000);
    cJSON_AddNumberToObject(plan,"deadline_epoch",charge_plan.deadline_s);
    cJSON_AddBoolToObject(o,"expert_mode",settings.expert_mode);
    cJSON_AddBoolToObject(o,"expert_parameters_modified",expert_modified(&expert_saved)||settings_gpio_modified(&saved_settings));
    cJSON_AddBoolToObject(o,"phase_mqtt_allowed",phase_mqtt_allowed);
    cJSON_AddBoolToObject(o,"phase_mqtt_ready",phase_mqtt_supported_locked());
    cJSON_AddBoolToObject(o,"phase_output_base_ready",wifi_online&&phase_switch_meter_supported(&settings));
    cJSON_AddBoolToObject(o,"phase_output_setup_allowed",!phase_test_active&&!restart_pending()&&phase_mqtt_stopped_locked());
    cJSON_AddBoolToObject(o,"phase_test_active",phase_test_active);
    cJSON_AddBoolToObject(o,"phase_test_ready",phase_test_ready_locked(now));
    cJSON_AddStringToObject(o,"phase_test_block",phase_test_block_locked(now));
    cJSON_AddNumberToObject(o,"phase_test_goal",phase_test_active?phase_test_goal:0);
    cJSON_AddBoolToObject(o,"phase_output_mqtt_ready",mqtt_online&&settings.mqtt_enabled);
    cJSON_AddNumberToObject(o,"phase_output_transport",saved_settings.phase_output_transport);
    cJSON_AddBoolToObject(o,"phase_output_meter_ready",phase_switch_meter_supported(&settings));
    cJSON_AddNumberToObject(o,"phase_output_measured_phases",phase_measured_currents(&settings));
    cJSON_AddBoolToObject(o,"phase_output_connected",phase_output_link_locked());
    cJSON_AddBoolToObject(o,"phase_mqtt_true_three_phase",saved_settings.phase_mqtt_true_three_phase);
    char phase_topic[128];
    if(phase_mqtt_output_topic(&saved_settings,phase_topic,sizeof(phase_topic)))cJSON_AddStringToObject(o,"phase_mqtt_output_topic",phase_topic);
    if(phase_mqtt_remote()&&phase_mqtt_known){
        cJSON_AddBoolToObject(o,"phase_mqtt_output_single",phase_mqtt_assumed==1);
        cJSON_AddBoolToObject(o,"phase_mqtt_output_on",phase_mqtt_output_value(&settings,phase_mqtt_assumed));
    }else{cJSON_AddNullToObject(o,"phase_mqtt_output_single");cJSON_AddNullToObject(o,"phase_mqtt_output_on");}
    cJSON_AddBoolToObject(o,"basic_mode",settings.basic_mode);
    cJSON_AddBoolToObject(o,"pv_allocation_enabled",settings.pv_allocation_enabled);
    cJSON_AddNumberToObject(o,"pv_priority",settings.pv_priority);
    cJSON_AddBoolToObject(o,"pv_allocation_ready",settings.pv_allocation_enabled && fresh(now,battery_charge_at,EMS_INPUT_TTL) && fresh(now,battery_discharge_at,EMS_INPUT_TTL) && fresh(now,battery_soc_at,EMS_INPUT_TTL));
    cJSON_AddBoolToObject(o,"vehicle_soc_enabled",settings.vehicle_soc_enabled);
    cJSON_AddBoolToObject(o,"mqtt_enabled",settings.mqtt_enabled);
    cJSON_AddBoolToObject(o,"homeassistant_enabled",settings.homeassistant_enabled);
    cJSON_AddStringToObject(o,"mqtt_input_source",settings.mqtt_input_source==2?"opendtu":settings.mqtt_input_source ? "homeassistant" : "iobroker");
    cJSON_AddBoolToObject(o,"pv_display_enabled",settings.pv_display_enabled);
    cJSON_AddBoolToObject(o,"relay_board_enabled",settings.relay_board_enabled);
    if(settings.relay_board_enabled) {
        cJSON_AddBoolToObject(o,"relay1_on",relay_on[0]);
    }
    cJSON_AddNumberToObject(o,"power_per_amp_kw",settings.charge_phases*settings.nominal_v*settings.power_factor/1000);
    cJSON_AddNumberToObject(o,"single_power_per_amp_kw",settings.nominal_v*settings.power_factor/1000);
    cJSON_AddNumberToObject(o,"phase_actuator_type",settings.phase_actuator_type);
    if(phase_shelly_remote()){
        cJSON_AddBoolToObject(o,"phase_shelly_connected",phase_shelly_observed_locked(now)!=0);
        cJSON_AddBoolToObject(o,"phase_shelly_config_ok",phase_shelly_config_ok);
        cJSON_AddBoolToObject(o,"phase_shelly_no_closed",phase_shelly_feedback_value.no_closed);
        cJSON_AddBoolToObject(o,"phase_shelly_nc_closed",phase_shelly_feedback_value.nc_closed);
        cJSON_AddBoolToObject(o,"phase_shelly_output",phase_shelly_feedback_value.output);
    }
    cJSON_AddBoolToObject(o,"phase_switch_enabled",settings.phase_switch_enabled);
    cJSON_AddBoolToObject(o,"phase_feedback_enabled",settings.phase_feedback_enabled);
    cJSON_AddNumberToObject(o,"phase_feedback_count",settings.phase_feedback_count);
    bool contact_values_ok=settings.phase_switch_enabled&&settings.phase_feedback_enabled&&
        (phase_shelly_remote()?(phase_shelly_config_ok&&fresh(now,phase_shelly_at,SHELLY_PHASE_TTL_MS)):
         settings.phase_actuator_type==0&&phase_gpio_ready);
    if(contact_values_ok){
        cJSON_AddBoolToObject(o,"phase_contact1_closed",phase_shelly_remote()?phase_shelly_feedback_value.no_closed:gpio_get_level(PHASE_FEEDBACK_GPIO)==0);
        if(settings.phase_feedback_count==2)
            cJSON_AddBoolToObject(o,"phase_contact2_closed",phase_shelly_remote()?phase_shelly_feedback_value.nc_closed:gpio_get_level(PHASE_FEEDBACK_SECOND_GPIO)==0);
        else cJSON_AddNullToObject(o,"phase_contact2_closed");
    }else{
        cJSON_AddNullToObject(o,"phase_contact1_closed");cJSON_AddNullToObject(o,"phase_contact2_closed");
    }
    cJSON_AddBoolToObject(o,"phase_feedback_closed_is_single",settings.phase_feedback_closed_is_single);
    cJSON_AddNumberToObject(o,"fixed_charge_phases",settings.fixed_charge_phases);
    cJSON_AddBoolToObject(o,"shell_limits_auto",settings.shell_limits_auto);
    cJSON_AddBoolToObject(o,"shell_limits_ok",settings.shell_limits_auto && fresh(now,shell_limits_at,900000));
    cJSON_AddStringToObject(o,"shell_setup_host",shell_active_host);
    cJSON_AddNumberToObject(o,"shell_limits_http",shell_limits_code);
    nullable(o,"shell_grid_limit_a",shell_grid_a,shell_limits_at>0);
    nullable(o,"shell_max_charge_a",shell_max_a,shell_limits_at>0);
    cJSON_AddNumberToObject(o,"charge_phases",settings.charge_phases);
    cJSON_AddNumberToObject(o,"manual_phases",settings.manual_phases);
    cJSON_AddNumberToObject(o,"phase_feedback",phase_gpio_ready?phase_observed():0);
    cJSON_AddBoolToObject(o,"phase_idle_inhibit",phase_idle_inhibit);
    cJSON_AddStringToObject(o,"phase_state",phase_state==PHASE_READY?"bereit":phase_state==PHASE_STOPPING?"stoppen":phase_state==PHASE_MOVING?"schalten":phase_state==PHASE_SETTLING?"pruefen":"fehler");
    /* Expose real state-machine deadlines; UI never invents a countdown. */
    int64_t phase_deadline=0; const char *phase_wait="none";
    phase_wait_status_locked(now,&phase_wait,&phase_deadline);
    cJSON_AddStringToObject(o,"phase_wait_kind",phase_wait);
    cJSON_AddNumberToObject(o,"phase_wait_s",phase_deadline>now?(phase_deadline-now+999)/1000:0);
    cJSON_AddBoolToObject(o,"control_status_visible",true);
    cJSON_AddBoolToObject(o,"can_unlock",phase_state==PHASE_FAULT?phase_can_unlock_locked(now):charge_guard.latched);
    cJSON_AddNumberToObject(o,"grid_price_kwh",settings.price_kwh);
    cJSON_AddNumberToObject(o,"solar_price_kwh",settings.solar_price_kwh);

    cJSON_AddBoolToObject(o,"enabled",settings.enabled); cJSON_AddStringToObject(o,"block_reason",block_reason_locked(now));

    cJSON_AddBoolToObject(o,"control_verified",settings.control_verified);

    cJSON_AddNumberToObject(o,"pv_wait_s",(pv_control.wait_ms+999)/1000);

    cJSON_AddNumberToObject(o,"min_charge_a",settings.min_charge_a);

    cJSON_AddBoolToObject(o,"charge_stop_latched",charge_guard.latched);
    cJSON_AddNumberToObject(o,"charge_retry_s",charge_guard.retry_until>now?(charge_guard.retry_until-now+999)/1000:0);
    cJSON_AddNumberToObject(o,"charge_stop_count",charge_guard.stop_count);
    cJSON_AddNumberToObject(o,"current_offset_a",settings.current_offset_a);
    cJSON_AddStringToObject(o,"calibration_state",current_calibration.since?"learning":current_calibration.adjustments?"adjusted":"waiting");

    cJSON_AddBoolToObject(o,"battery_protect",settings.battery_protect);
    cJSON_AddBoolToObject(o,"battery_use",battery_use);
    cJSON_AddBoolToObject(o,"battery_start_use",battery_start_use);
    cJSON_AddBoolToObject(o,"battery_buffer_active",battery_buffer.active);
    cJSON_AddBoolToObject(o,"battery_buffer_expired",battery_buffer.started && now-battery_buffer.started_at>=EMS_BATTERY_BUFFER_MS);
    cJSON_AddNumberToObject(o,"battery_buffer_duration_s",EMS_BATTERY_BUFFER_MS/1000);
    int64_t cloud_left=battery_buffer.started?EMS_BATTERY_BUFFER_MS-(now-battery_buffer.started_at):0;
    cJSON_AddNumberToObject(o,"battery_buffer_remaining_s",cloud_left>0?(cloud_left+999)/1000:0);
    cJSON_AddBoolToObject(o,"battery_assist_active",battery_buffer.assist_active);
    cJSON_AddBoolToObject(o,"battery_power_ok",battery_power_locked(now).fresh);
    cJSON_AddBoolToObject(o,"control_storage_ok",control_storage_ok);
    cJSON_AddNumberToObject(o,"battery_reserve_soc",settings.battery_reserve_soc);
    cJSON_AddNumberToObject(o,"battery_capacity_kwh",EMS_BATTERY_CAPACITY_KWH);
    cJSON_AddNumberToObject(o,"battery_discharge_limit_w",settings.battery_assist_limit_w);
    cJSON_AddNumberToObject(o,"battery_cloud_limit_w",settings.battery_cloud_limit_w);

    cJSON_AddBoolToObject(o,"battery_ok",battery_ready_locked(now));
    cJSON_AddBoolToObject(o,"battery_soc_ok",fresh(now,battery_soc_at,EMS_INPUT_TTL));

    nullable(o,"battery_soc_pct",battery_soc,fresh(now,battery_soc_at,EMS_INPUT_TTL));
    nullable(o,"car_soc_pct",car_soc,settings.vehicle_soc_enabled && fresh(now,car_soc_at,ems_param_ms(EP_CAR_TTL)));
    nullable(o,"pv_generation_w",pv_generation_w,settings.pv_display_enabled && fresh(now,pv_generation_at,EMS_INPUT_TTL));
    battery_power_t battery_power=battery_power_locked(now);
    nullable(o,"battery_charge_w",battery_power.charge_w,battery_power.fresh);
    nullable(o,"battery_discharge_w",battery_power.discharge_w,battery_power.fresh);

    cJSON_AddNumberToObject(o,"pv_start_threshold_a",battery_start_use&&settings.battery_protect&&battery_ready_locked(now)&&battery_soc>settings.battery_reserve_soc?active_min_a():pv_start_threshold(&settings));

    cJSON_AddNumberToObject(o,"pv_stop_threshold_a",active_min_a()-ems_param(EP_PV_DEFICIT));

    cJSON_AddBoolToObject(o,"meter_ok",meter_ok); cJSON_AddBoolToObject(o,"wallbox_ok",fresh(now,wallbox_at,ems_param_ms(EP_SHELL_TTL)));

    cJSON_AddBoolToObject(o,"feedback_ok",actual_ok); cJSON_AddBoolToObject(o,"mqtt_ok",mqtt_online); cJSON_AddBoolToObject(o,"wifi_ok",wifi_online);
    nullable(o,"wifi_rssi_dbm",access_point.rssi,wifi_signal_ok);

    cJSON_AddBoolToObject(o,"wallbox_uart_ready",wallbox_ready); cJSON_AddBoolToObject(o,"meter_uart_ready",meter_ready);

    cJSON_AddBoolToObject(o,"reboot_required",reboot_required||expert_reboot_pending);
    cJSON_AddBoolToObject(o,"firmware_check_pending",boot_validation_pending);
    cJSON_AddBoolToObject(o,"firmware_check_ok",boot_validation_ok);
    cJSON_AddBoolToObject(o,"firmware_check_failed",boot_validation_failed);
    cJSON_AddBoolToObject(o,"ota_rollback_armed",boot_rollback_armed); cJSON_AddBoolToObject(o,"clock_ok",date!=0);

    cJSON_AddNumberToObject(o,"current_date",date);

    cJSON_AddBoolToObject(o,"storage_ok",stats_storage_ok && config_storage_ok && !storage_error && plan_storage_ok && control_storage_ok);

    cJSON_AddNumberToObject(o,"uptime_s",now/1000);
    cJSON_AddNumberToObject(o,"min_free_heap",heap_caps_get_minimum_free_size(MALLOC_CAP_8BIT));
    cJSON_AddNumberToObject(o,"largest_free_block",heap_caps_get_largest_free_block(MALLOC_CAP_8BIT));cJSON_AddNumberToObject(o,"reset_reason",esp_reset_reason());

    cJSON_AddNumberToObject(o,"meter_failures",meter_failures); cJSON_AddNumberToObject(o,"wallbox_requests",wallbox_requests);

    cJSON_AddNumberToObject(o,"wallbox_rx_bytes",wallbox_rx_bytes); cJSON_AddNumberToObject(o,"wallbox_valid_frames",wallbox_valid_frames);

    cJSON_AddNumberToObject(o,"wallbox_other_address",wallbox_other_address); cJSON_AddNumberToObject(o,"wallbox_crc_resyncs",wallbox_crc_resyncs);

    nullable(o,"wallbox_last_address",wallbox_last_address,wallbox_valid_frames>0); nullable(o,"wallbox_last_function",wallbox_last_function,wallbox_valid_frames>0);

    nullable(o,"wallbox_last_register",wallbox_last_register,wallbox_valid_frames>0);

    nullable(o,"wallbox_last_count",wallbox_last_count,wallbox_valid_frames>0);

    nullable(o,"wallbox_reply_age_s",(now-wallbox_at)/1000,wallbox_at>0);

    cJSON_AddNumberToObject(o,"wallbox_tx_errors",wallbox_tx_errors);

    char rx_hex[sizeof(wallbox_rx_tail)*2+1]; const char *hex="0123456789ABCDEF";

    for(size_t i=0;i<wallbox_rx_count;i++) {

        uint8_t byte=wallbox_rx_tail[(wallbox_rx_next+sizeof(wallbox_rx_tail)-wallbox_rx_count+i)%sizeof(wallbox_rx_tail)];

        rx_hex[i*2]=hex[byte>>4]; rx_hex[i*2+1]=hex[byte&15];

    }

    rx_hex[wallbox_rx_count*2]=0; cJSON_AddStringToObject(o,"wallbox_rx_tail_hex",rx_hex);

    nullable(o,"wallbox_rx_age_ms",now-wallbox_rx_at,wallbox_rx_count>0);

    cJSON_AddNumberToObject(o,"mqtt_rejected",mqtt_rejected); cJSON_AddNumberToObject(o,"saved_ago_s",(now-last_saved_ms)/1000);

    cJSON_AddNumberToObject(o,"grid_limit_a",settings.grid_limit_a); cJSON_AddNumberToObject(o,"max_charge_a",settings.max_charge_a);

    cJSON_AddNumberToObject(o,"current_a",settings.manual_current_a); cJSON_AddNumberToObject(o,"target_current_a",target_locked(now));
    cJSON_AddNumberToObject(o,"manual_power_kw",settings.manual_current_a*settings.manual_phases*settings.nominal_v*settings.power_factor/1000);

    cJSON_AddBoolToObject(o,"stop_requested",target_locked(now)<=0);

    cJSON_AddBoolToObject(o,"stop_current_below_1a",target_locked(now)<=0 && actual_ok && actual_a[0]<1 && actual_a[1]<1 && actual_a[2]<1);

    cJSON_AddItemToObject(o,"last_sent_a",number_array(last_sent_a,3,last_sent_current_at>0));

    nullable(o,"last_sent_age_ms",now-last_sent_current_at,last_sent_current_at>0);

    cJSON_AddNumberToObject(o,"pv_surplus_a",settings.pv_surplus_a); cJSON_AddStringToObject(o,"station_ip",station_ip);

    cJSON_AddNumberToObject(o,"xemex_coils",settings.xemex_coils);

    cJSON_AddStringToObject(o,"wallbox_meter_type",settings.wallbox_meter_type);

    cJSON_AddStringToObject(o,"wallbox_power_source",(sdm_meter()||network_wallbox_meter())?settings.wallbox_meter_type:"mqtt");

    bool extrapolated=(sdm_meter()&&sdm_profile(settings.wallbox_meter_type)->phases==1)||(!strcmp(settings.wallbox_meter_type,"xemex")&&settings.xemex_coils<3);
    cJSON_AddBoolToObject(o,"power_is_extrapolated",extrapolated);

    cJSON_AddBoolToObject(o,"house_meter_ok",house_power_capable()?fresh(now,house_power_at,house_meter_ttl()):fresh(now,house_current_at,house_meter_ttl()));
    cJSON_AddBoolToObject(o,"house_power_ok",fresh(now,house_power_at,house_meter_ttl()));
    bool house_phases_ok=house_currents_ready_locked(now);
    cJSON_AddBoolToObject(o,"house_current_ok",house_phases_ok);
    cJSON_AddBoolToObject(o,"grid_guard_enabled",settings.grid_guard_enabled);
    cJSON_AddBoolToObject(o,"grid_guard_ok",!settings.grid_guard_enabled || house_phases_ok);
    cJSON_AddBoolToObject(o,"grid_guard_fallback",settings.grid_guard_enabled && !house_phases_ok);
    nullable(o,"grid_guard_limit_kw",EMS_GRID_FALLBACK_W/1000,settings.grid_guard_enabled && !house_phases_ok);
    cJSON_AddItemToObject(o,"house_a",number_array(house_a,3,house_phases_ok));
    cJSON_AddBoolToObject(o,"external_mode_input_enabled",settings.external_mode_input_enabled);
    cJSON_AddBoolToObject(o,"external_mode_contact",settings.external_mode_input_enabled && mode_contact.stable);
    cJSON_AddBoolToObject(o,"evu_input_enabled",settings.evu_input_enabled);
    cJSON_AddBoolToObject(o,"evu_active",settings.evu_input_enabled && evu_contact.stable);
    cJSON_AddNumberToObject(o,"evu_limit_a",settings.evu_limit_a);

    cJSON_AddBoolToObject(o,"ap_active",wifi_mode==WIFI_MODE_AP || wifi_mode==WIFI_MODE_APSTA);

    cJSON_AddBoolToObject(o,"ap_forced",wifi_ap_forced);

    cJSON_AddStringToObject(o,"ap_ssid",SETUP_SSID);
    cJSON_AddStringToObject(o,"hostname",DEVICE_HOSTNAME);

    cJSON_AddBoolToObject(o,"wifi_fallback",wifi_fallback);

    nullable(o,"meter_total_a",settings.charge_phases==1?meter_a[0]:meter_a[0]+meter_a[1]+meter_a[2],meter_ok);

    cJSON_AddBoolToObject(o,"zero_feed_enabled",settings.zero_feed_enabled);

    cJSON_AddStringToObject(o,"house_meter_type",settings.house_meter_type);

    nullable(o,"house_power_w",house_power_w,fresh(now,house_power_at,house_meter_ttl()));

    cJSON_AddStringToObject(o,"mqtt_prefix",settings.mqtt_prefix);

    cJSON_AddItemToObject(o,"meter_a",number_array(meter_a,3,meter_ok)); cJSON_AddItemToObject(o,"actual_a",number_array(actual_a,3,actual_ok));

    cJSON_AddItemToObject(o,"reported_a",number_array(reported,3,true));

    nullable(o,"estimate_w",estimated_charge_power(&settings,meter_a),meter_ok);

    nullable(o,"wallbox_w",wallbox_w,power_ok);

    cJSON *sources=cJSON_AddArrayToObject(o,"energy");

    for(int s=0;s<2;s++) {

        cJSON *item=cJSON_CreateObject(); double today=0; uint64_t covered=0,charging=0;

        for(int d=0;d<EMS_DAYS;d++) if(energy.store.days[d].date==date && date) { today=energy.store.days[d].wh[s]; covered=energy.store.days[d].covered_ms[s]; charging=energy.store.days[d].charging_ms; }

        cJSON_AddStringToObject(item,"source",s==0?"current_estimate":sdm_meter()?settings.wallbox_meter_type:"wallbox_mqtt");

        cJSON_AddNumberToObject(item,"total_kwh",fmax(0,energy.store.total_wh[s]-energy_reset_wh[s])/1000); cJSON_AddNumberToObject(item,"boot_kwh",energy.boot_wh[s]/1000);
        cJSON_AddNumberToObject(item,"total_reset_date",energy_reset_date);

        cJSON_AddNumberToObject(item,"undated_kwh",energy.store.undated_wh[s]/1000);

        nullable(item,"today_kwh",today/1000,date!=0 && covered>0); cJSON_AddNumberToObject(item,"today_covered_s",covered/1000);
        cJSON_AddNumberToObject(item,"today_charging_s",charging/1000);

        nullable(item,"today_cost",today/1000*settings.price_kwh,date!=0 && covered>0); cJSON_AddItemToArray(sources,item);

    }

    if(include_token) cJSON_AddStringToObject(o,"token",csrf_token);

    UNLOCK(); return o;

}

static void publish_value(const char *name,cJSON *value) {

    char topic[160]; snprintf(topic,sizeof(topic),"%s/sensor/%s",settings.mqtt_prefix,name);

    char *body=value?cJSON_PrintUnformatted(value):NULL;

    /* QoS 0 needs store=true with enqueue; otherwise ESP-MQTT drops it. */
    esp_mqtt_client_enqueue(mqtt_client,topic,body?body:"null",0,0,0,true); free(body);

}

static cJSON *homeassistant_entity(const char *name,const char *object,const char *state_suffix) {
    cJSON *o=cJSON_CreateObject(); if(!o) return NULL;
    char topic[192],unique[96];
    cJSON_AddStringToObject(o,"name",name);
    snprintf(unique,sizeof(unique),"%s_%s",mqtt_device_id,object); cJSON_AddStringToObject(o,"unique_id",unique);
    if(state_suffix) { snprintf(topic,sizeof(topic),"%s/%s",settings.mqtt_prefix,state_suffix); cJSON_AddStringToObject(o,"state_topic",topic); }
    snprintf(topic,sizeof(topic),"%s/availability",settings.mqtt_prefix); cJSON_AddStringToObject(o,"availability_topic",topic);
    cJSON *device=cJSON_AddObjectToObject(o,"device");
    cJSON *identifiers=cJSON_AddArrayToObject(device,"identifiers"); cJSON_AddItemToArray(identifiers,cJSON_CreateString(mqtt_device_id));
    cJSON_AddStringToObject(device,"name","TeeNet"); cJSON_AddStringToObject(device,"manufacturer","TeeNet");
    cJSON_AddStringToObject(device,"model","ESP32-S3 Wallbox EMS"); cJSON_AddStringToObject(device,"sw_version",VERSION);
    if(station_ip[0]) { char url[48]; snprintf(url,sizeof(url),"http://%s/",station_ip); cJSON_AddStringToObject(device,"configuration_url",url); }
    cJSON *origin=cJSON_AddObjectToObject(o,"origin"); cJSON_AddStringToObject(origin,"name","TeeNet"); cJSON_AddStringToObject(origin,"sw_version",VERSION);
    return o;
}

static void homeassistant_publish_entity(const char *component,const char *object,cJSON *entity,bool enabled) {
    if(!mqtt_client) { cJSON_Delete(entity); return; }
    char topic[192]; snprintf(topic,sizeof(topic),"homeassistant/%s/%s/%s/config",component,mqtt_device_id,object);
    char *body=enabled&&entity?cJSON_PrintUnformatted(entity):NULL;
    esp_mqtt_client_enqueue(mqtt_client,topic,body?body:"",0,1,1,false);
    free(body); cJSON_Delete(entity);
}

static void homeassistant_discovery(bool enabled) {
    static const struct { const char *component,*object; } entities[]={
        {"sensor","charging_power"},{"sensor","house_power"},{"binary_sensor","wallbox"},
        {"binary_sensor","meter"},{"select","mode"},{"number","charging_power_setpoint"},
        {"switch","cloud_buffer"},{"switch","battery_assist"},{"sensor","battery_soc"},
        {"sensor","pv_generation"},{"sensor","battery_charge"},{"sensor","battery_discharge"}};
    if(!enabled) {
        for(unsigned i=0;i<sizeof(entities)/sizeof(entities[0]);i++)
            homeassistant_publish_entity(entities[i].component,entities[i].object,NULL,false);
        return;
    }
    cJSON *o=homeassistant_entity("Ladeleistung","charging_power","sensor/estimate_w");
    cJSON_AddStringToObject(o,"device_class","power");cJSON_AddStringToObject(o,"state_class","measurement");cJSON_AddStringToObject(o,"unit_of_measurement","W");
    homeassistant_publish_entity("sensor","charging_power",o,true);
    o=homeassistant_entity("Hausanschluss","house_power","sensor/house_power_w");
    cJSON_AddStringToObject(o,"device_class","power");cJSON_AddStringToObject(o,"state_class","measurement");cJSON_AddStringToObject(o,"unit_of_measurement","W");
    homeassistant_publish_entity("sensor","house_power",o,true);
    o=homeassistant_entity("Wallbox-Kommunikation","wallbox","sensor/wallbox_ok");
    cJSON_AddStringToObject(o,"payload_on","true");cJSON_AddStringToObject(o,"payload_off","false");cJSON_AddStringToObject(o,"device_class","connectivity");
    homeassistant_publish_entity("binary_sensor","wallbox",o,true);
    o=homeassistant_entity("Zählerdaten","meter","sensor/meter_ok");
    cJSON_AddStringToObject(o,"payload_on","true");cJSON_AddStringToObject(o,"payload_off","false");cJSON_AddStringToObject(o,"device_class","connectivity");
    homeassistant_publish_entity("binary_sensor","meter",o,true);
    o=homeassistant_entity("Betriebsart","mode","sensor/mode");
    char topic[160];snprintf(topic,sizeof(topic),"%s/command/mode",settings.mqtt_prefix);cJSON_AddStringToObject(o,"command_topic",topic);
    cJSON *options=cJSON_AddArrayToObject(o,"options");cJSON_AddItemToArray(options,cJSON_CreateString("off"));cJSON_AddItemToArray(options,cJSON_CreateString("manual"));cJSON_AddItemToArray(options,cJSON_CreateString("pv"));
    homeassistant_publish_entity("select","mode",o,true);
    o=homeassistant_entity("Gewünschte Ladeleistung","charging_power_setpoint","sensor/manual_power_kw");
    snprintf(topic,sizeof(topic),"%s/command/power_kw",settings.mqtt_prefix);cJSON_AddStringToObject(o,"command_topic",topic);
    cJSON_AddNumberToObject(o,"min",(settings.phase_switch_enabled || settings.fixed_charge_phases==1)?2.0:6.0);
    cJSON_AddNumberToObject(o,"max",floorf(settings.max_charge_a*(settings.phase_switch_enabled?3:settings.fixed_charge_phases)*settings.nominal_v*settings.power_factor/500)/2);
    cJSON_AddNumberToObject(o,"step",0.5);cJSON_AddStringToObject(o,"unit_of_measurement","kW");cJSON_AddStringToObject(o,"mode","slider");
    homeassistant_publish_entity("number","charging_power_setpoint",o,true);
    o=homeassistant_entity("Akku-Wolkenpuffer","cloud_buffer","sensor/battery_use");
    snprintf(topic,sizeof(topic),"%s/command/battery_use",settings.mqtt_prefix);cJSON_AddStringToObject(o,"command_topic",topic);cJSON_AddStringToObject(o,"payload_on","true");cJSON_AddStringToObject(o,"payload_off","false");
    homeassistant_publish_entity("switch","cloud_buffer",o,true);
    o=homeassistant_entity("Auto aus Hausakku laden","battery_assist","sensor/battery_start_use");
    snprintf(topic,sizeof(topic),"%s/command/battery_start_use",settings.mqtt_prefix);cJSON_AddStringToObject(o,"command_topic",topic);cJSON_AddStringToObject(o,"payload_on","true");cJSON_AddStringToObject(o,"payload_off","false");
    homeassistant_publish_entity("switch","battery_assist",o,true);
    o=homeassistant_entity("Hausakku Ladezustand","battery_soc","sensor/battery_soc_pct");
    cJSON_AddStringToObject(o,"device_class","battery");cJSON_AddStringToObject(o,"state_class","measurement");cJSON_AddStringToObject(o,"unit_of_measurement","%");
    homeassistant_publish_entity("sensor","battery_soc",o,true);
    o=homeassistant_entity("PV-Erzeugung","pv_generation","sensor/pv_generation_w");
    cJSON_AddStringToObject(o,"device_class","power");cJSON_AddStringToObject(o,"state_class","measurement");cJSON_AddStringToObject(o,"unit_of_measurement","W");
    homeassistant_publish_entity("sensor","pv_generation",o,true);
    o=homeassistant_entity("Hausakku lädt","battery_charge","sensor/battery_charge_w");
    cJSON_AddStringToObject(o,"device_class","power");cJSON_AddStringToObject(o,"state_class","measurement");cJSON_AddStringToObject(o,"unit_of_measurement","W");
    homeassistant_publish_entity("sensor","battery_charge",o,true);
    o=homeassistant_entity("Hausakku entlädt","battery_discharge","sensor/battery_discharge_w");
    cJSON_AddStringToObject(o,"device_class","power");cJSON_AddStringToObject(o,"state_class","measurement");cJSON_AddStringToObject(o,"unit_of_measurement","W");
    homeassistant_publish_entity("sensor","battery_discharge",o,true);
}

static void mqtt_state(void) {

    LOCK(); bool online=mqtt_online; UNLOCK();

    if(!online || !mqtt_client || esp_mqtt_client_get_outbox_size(mqtt_client)>8000) return;

    cJSON *o=status_json(false); if(!o) return;

    if(settings.mqtt_state_json) {

        char *body=cJSON_PrintUnformatted(o); char topic[100]; snprintf(topic,sizeof(topic),"%s/state",settings.mqtt_prefix);

        if(body) esp_mqtt_client_enqueue(mqtt_client,topic,body,0,1,1,false);

        free(body);

    }

    /* Publish useful TeeNet outputs only. ioBroker-origin values are already there. */
    const char *names[]={"estimate_w","target_current_a","current_a","manual_power_kw","house_power_w","meter_ok","house_meter_ok","wallbox_ok","enabled","mode","block_reason","battery_use","battery_start_use","charge_stop_latched"};

    for(unsigned i=0;i<sizeof(names)/sizeof(names[0]);i++) {

        cJSON *value=cJSON_GetObjectItemCaseSensitive(o,names[i]);

        if(cJSON_IsString(value)) {

            char topic[160]; snprintf(topic,sizeof(topic),"%s/sensor/%s",settings.mqtt_prefix,names[i]);

            esp_mqtt_client_enqueue(mqtt_client,topic,value->valuestring,0,0,0,true);

        } else publish_value(names[i],value);

    }

    static bool phase_topics_published;
    if(phase_mqtt_allowed || phase_topics_published) {
        const char *phase_names[]={"phase_mqtt_allowed","charge_phases"};
        for(unsigned i=0;i<sizeof(phase_names)/sizeof(phase_names[0]);i++)
            publish_value(phase_names[i],cJSON_GetObjectItemCaseSensitive(o,phase_names[i]));
    }
    phase_topics_published=phase_mqtt_allowed;

    if(settings.homeassistant_enabled) {
        const char *homeassistant_values[]={"battery_soc_pct","pv_generation_w","battery_charge_w","battery_discharge_w"};
        for(unsigned i=0;i<sizeof(homeassistant_values)/sizeof(homeassistant_values[0]);i++)
            publish_value(homeassistant_values[i],cJSON_GetObjectItemCaseSensitive(o,homeassistant_values[i]));
    }

    static unsigned energy_cycle=0;
    if(++energy_cycle>=3) {
        energy_cycle=0;
        const char *keys[]={"total_kwh","today_kwh","today_cost"};
        for(int k=0;k<3;k++) {
            char name[64]; snprintf(name,sizeof(name),"energy/estimate_%s",keys[k]);
            publish_value(name,cJSON_GetObjectItem(cJSON_GetArrayItem(cJSON_GetObjectItem(o,"energy"),0),keys[k]));
        }
    }

    cJSON_Delete(o);

}

static bool apply_control(cJSON *o) {
    if(!terms_accepted)return false;

    if(!cJSON_IsObject(o) || !o->child) return false;

    int64_t command_epoch,command_midnight;uint32_t command_date=calendar(&command_epoch,&command_midnight);
    LOCK();
    if(evcc_configuring || phase_mqtt_saving){UNLOCK();return false;}
    if(evcc_control.configured) {
        char *body=cJSON_PrintUnformatted(o);
        bool stop=evcc_emergency_stop_json(body);free(body);
        if(!stop){UNLOCK();return false;}
        evcc_local_stop(&evcc_control);
    }
    settings_t next=settings; bool valid=true,pv=false,next_battery_use=battery_use,next_battery_start_use=battery_start_use,restart=false;
    bool manual_phases_explicit=false,manual_mode_requested=false,persist_preferences=false;
    int64_t change_at=now_ms();float previous_target=target_locked(change_at);control_mode_t previous_mode=settings.mode;
    bool transition=false;
    bool user_override=cJSON_GetObjectItem(o,"mode")||cJSON_GetObjectItem(o,"enabled")||cJSON_GetObjectItem(o,"current_a");

    for(cJSON *i=o->child;i;i=i->next) {

        if(!strcmp(i->string,"mode")) { valid=cJSON_IsString(i) && parse_mode(i->valuestring,&next.mode); if(valid && next.mode==MODE_MANUAL) manual_mode_requested=true; }
        else if(!strcmp(i->string,"manual_phases")) { valid=cJSON_IsNumber(i) && (i->valuedouble==1 || i->valuedouble==3) && (i->valueint==(settings.fixed_charge_phases) || settings.phase_switch_enabled); if(valid){next.manual_phases=i->valueint;manual_phases_explicit=true;} }

        else if(!strcmp(i->string,"restart")) { valid=cJSON_IsBool(i); restart=cJSON_IsTrue(i); }
        else if(!strcmp(i->string,"battery_use")) { valid=cJSON_IsBool(i); next_battery_use=cJSON_IsTrue(i); }
        else if(!strcmp(i->string,"battery_start_use")) { valid=cJSON_IsBool(i); next_battery_start_use=cJSON_IsTrue(i); }
        else if(!strcmp(i->string,"enabled")) { valid=cJSON_IsBool(i); next.enabled=cJSON_IsTrue(i); }

        else if(!strcmp(i->string,"current_a") || !strcmp(i->string,"pv_surplus_a")) {

            valid=cJSON_IsNumber(i) && isfinite(i->valuedouble) && i->valuedouble>=0 && i->valuedouble<=next.max_charge_a;

            if(valid && !strcmp(i->string,"current_a")) next.manual_current_a=i->valuedouble;

            else if(valid) { next.pv_surplus_a=i->valuedouble; pv=true; }

        } else valid=false;

        if(!valid) break;

    }

    if(manual_mode_requested && !manual_phases_explicit) next.manual_phases=next.phase_switch_enabled?settings.charge_phases:next.fixed_charge_phases;
    if(manual_mode_requested && previous_mode==MODE_PV && previous_target>0 && !cJSON_GetObjectItem(o,"current_a"))next.manual_current_a=previous_target;
    if(!next.phase_switch_enabled && next.manual_phases!=next.fixed_charge_phases)valid=false;
    if(settings.basic_mode && (next.mode==MODE_PV || next.mode==MODE_GRID_LIMIT || pv || next_battery_use || next_battery_start_use))valid=false;
    if((next_battery_use || next_battery_start_use) && (!settings.battery_protect || !settings.zero_feed_enabled)) valid=false;
    if(next.mode==MODE_OFF) next.enabled=false;
    if(restart && phase_state==PHASE_FAULT && !phase_can_unlock_locked(now_ms())) valid=false;

    if(valid && (!pv || !settings.zero_feed_enabled) && !restarting && !ota_in_progress && ((!phase_test_active&&!phase_config_saving) || !next.enabled) && (!restart_pending() || !next.enabled)) {

        /* Restore before changing preferences or starting a controller. A
           later persistence pass must not overwrite newly started timers. */
        restore_control_memory_locked(change_at,command_date?command_epoch:0,true);

        transition=next.mode!=settings.mode || next.enabled!=settings.enabled;
        if(phase_test_active&&user_override&&!next.enabled)phase_test_end_locked();

        if((restart && charge_guard.latched) || (next.enabled && !settings.enabled)) memset(&charge_guard,0,sizeof(charge_guard));
        if(restart && phase_can_unlock_locked(now_ms())) {
            phase_goal=phase_shelly_remote()?phase_observed():3;
            if(phase_shelly_remote()){phase_shelly_uncertain=false;phase_shelly_pending=false;}
            if(phase_mqtt_remote()){
                phase_mqtt_pending=false;phase_mqtt_known=false;phase_goal=3;
                phase_stop_at=now_ms();phase_zero_at=0;phase_state=PHASE_STOPPING;
            }else {settings.charge_phases=phase_goal;phase_move_at=now_ms();phase_state=PHASE_SETTLING;}
        }
        if(phase_idle_inhibit && next.enabled &&
           (restart || !settings.enabled || next.mode!=settings.mode ||
            next.manual_phases!=settings.manual_phases ||
            fabsf(next.manual_current_a-settings.manual_current_a)>=0.05f)) {
            phase_idle_inhibit=false; phase_hold_until=0;
        }

        if(next_battery_use!=battery_use || next_battery_start_use!=battery_start_use) {
            persist_preferences=true;
            battery_use=next_battery_use;
            battery_start_use=next_battery_start_use;
            if(!battery_use) {battery_buffer.started=false;battery_buffer.active=false;battery_buffer.remaining_ms=0;}
            if(settings.zero_feed_enabled && fresh(now_ms(),house_power_at,house_meter_ttl()) && fresh(now_ms(),actual_at,wallbox_meter_ttl())) {
                int64_t now=now_ms();
                settings.pv_surplus_a=pv_available_current_locked(now,house_power_w);
                pv_at=now_ms(); next.pv_surplus_a=settings.pv_surplus_a;
                /* Recalculate availability immediately, but let the regular
                   PV controller ramp down and apply its 30-second deficit
                   grace period. A preference toggle is not an emergency stop.
                   SOC limits and stale telemetry still block independently. */
            }
        }

        settings.mode=next.mode; settings.enabled=next.enabled;

        settings.manual_current_a=next.manual_current_a; settings.manual_phases=next.manual_phases; settings.pv_surplus_a=next.pv_surplus_a;
        if(transition)mode_transition_locked(previous_mode,previous_target,change_at);
        if(user_override && charge_plan.active){charge_plan.active=false;plan_phase=PLAN_OFF;plan_dirty=true;plan_revision++;event_locked("plan_cancelled");}
        if(user_override)event_locked(next.enabled?"control_changed":"user_stop");

        if(pv) pv_at=now_ms();

    }

    else valid=false;

    UNLOCK();
    if(valid && persist_preferences) {
        if(xSemaphoreTake(maintenance_flash_lock,ticks(2000))==pdTRUE){valid=save_control_memory(true);xSemaphoreGive(maintenance_flash_lock);}
        else valid=false;
    }
    return valid;

}

static bool mqtt_input(const char *suffix,const char *payload) {

    if(!settings.mqtt_enabled) return false;
    /* Retired command/phases and outgoing boolean topics have no input handler. */
    if(settings.mqtt_input_source==2 && (!strncmp(suffix,"input/battery_",14)||!strncmp(suffix,"input/pv_generation",19)))return true;
    if((!settings.battery_protect || (settings.huawei_enabled && settings.huawei_battery)) && !strncmp(suffix,"input/battery_",14))return true;
    if((!settings.pv_display_enabled || (settings.huawei_enabled && settings.huawei_pv)) && !strncmp(suffix,"input/pv_generation",19))return true;
    double value;
    if(!strcmp(suffix,"input/car_soc_valid")) {
        if(strcmp(payload,"false") && strcmp(payload,"0")) return false;
        LOCK(); car_soc_at=0; UNLOCK(); return true;
    }
    if(!strcmp(suffix,"input/car_soc_pct")) {
        if(!parse_number(payload,0,100,&value)) return false;
        LOCK(); if(settings.vehicle_soc_enabled) { car_soc=value; car_soc_at=now_ms(); } else car_soc_at=0; UNLOCK(); return true;
    }
    if(!strcmp(suffix,"input/battery_soc_valid")) {
        if(strcmp(payload,"false") && strcmp(payload,"0")) return false;
        LOCK(); battery_soc_at=0; UNLOCK(); return true;
    }
    if(!strcmp(suffix,"input/battery_soc_pct")) {
        if(!parse_number(payload,0,100,&value)) return false;
        LOCK(); battery_soc=value; battery_soc_at=now_ms(); UNLOCK(); return true;
    }
    if(!strcmp(suffix,"input/pv_generation_valid")) {
        if(strcmp(payload,"false") && strcmp(payload,"0")) return false;
        LOCK(); pv_generation_at=0; UNLOCK(); return true;
    }
    if(!strcmp(suffix,"input/battery_power_valid")) {
        if(strcmp(payload,"false") && strcmp(payload,"0")) return false;
        LOCK(); battery_charge_at=0; battery_discharge_at=0; UNLOCK(); return true;
    }
    if(!strcmp(suffix,"input/pv_generation_w")) {
        if(!parse_number(payload,0,25000,&value)) return false;
        LOCK(); pv_generation_w=value; pv_generation_at=now_ms(); UNLOCK(); return true;
    }
    if(!strcmp(suffix,"input/battery_charge_w")) {
        if(!parse_number(payload,0,12000,&value)) return false;
        LOCK(); battery_charge_w=value; battery_charge_at=now_ms(); UNLOCK(); return true;
    }
    if(!strcmp(suffix,"input/battery_discharge_w")) {
        if(!parse_number(payload,0,12000,&value)) return false;
        LOCK(); battery_discharge_w=value; battery_discharge_at=now_ms(); UNLOCK(); return true;
    }

    if(!strcmp(suffix,"sensor/wallbox_power_w")) {

        if(sdm_meter()) return false;

        if(!parse_number(payload,0,50000,&value)) return false;

        LOCK(); if(!network_wallbox_meter()){wallbox_w=value; power_at=now_ms();} UNLOCK(); return true;

    }



    if(strncmp(suffix,"command/",8)) return false;

    const char *key=suffix+8; cJSON *o=cJSON_CreateObject(); if(!o) return false; bool parsed=true;

    if(!strcmp(key,"mode")) {
        control_mode_t mode;parsed=parse_mode(payload,&mode) && mode!=MODE_GRID_LIMIT;
        if(parsed){cJSON_AddStringToObject(o,key,payload);cJSON_AddBoolToObject(o,"enabled",mode!=MODE_OFF);}
    }

    else if(!strcmp(key,"enabled") || !strcmp(key,"battery_use") || !strcmp(key,"battery_start_use")) {

        parsed=!strcmp(payload,"true") || !strcmp(payload,"false") || !strcmp(payload,"1") || !strcmp(payload,"0");

        cJSON_AddBoolToObject(o,key,!strcmp(payload,"true") || !strcmp(payload,"1"));

    } else if(!strcmp(key,"power_kw")) {
        parsed=parse_number(payload,0,11,&value);
        if(parsed){
            double half=round(value*2)/2;
            unsigned phases=settings.phase_switch_enabled?(value>0 && value<=3.5?1:3):settings.fixed_charge_phases;
            double minimum=phases==1?2.0:6.0;
            parsed=(value==0 || (value>=minimum && (phases==3 || value<=3.5) && fabs(value-half)<.001)) &&
                half<=settings.max_charge_a*phases*settings.nominal_v*settings.power_factor/1000.0+.05;
            if(parsed){
                double current=value==0?0:fmax(settings.min_charge_a,
                    half*1000/(phases*settings.nominal_v*settings.power_factor));
                parsed=current<=settings.max_charge_a+.001;
                if(parsed){cJSON_AddStringToObject(o,"mode","manual");cJSON_AddNumberToObject(o,"current_a",fmin(current,settings.max_charge_a));cJSON_AddNumberToObject(o,"manual_phases",phases);cJSON_AddBoolToObject(o,"enabled",value>0);}
            }
        }
    } else if(!strcmp(key,"current_a") || !strcmp(key,"pv_surplus_a")) {

        parsed=parse_number(payload,0,63,&value);
        if(parsed) {
            cJSON_AddNumberToObject(o,key,value);
            if(!strcmp(key,"current_a")) {
                cJSON_AddStringToObject(o,"mode","manual");
                cJSON_AddBoolToObject(o,"enabled",true);
            }
        }

    } else parsed=false;

    bool ok=parsed && apply_control(o); cJSON_Delete(o); return ok;

}

#include "opendtu_runtime.inc"
static void mqtt_event(void *arg,esp_event_base_t base,int32_t id,void *data) {

    esp_mqtt_event_handle_t e=data;

    static char topic[192],payload[512]; static int received,expected;static bool retained;

    if(id==MQTT_EVENT_CONNECTED) {
        LOCK();if(settings.phase_output_transport==0)phase_mqtt_seen_connection=true;UNLOCK();

        LOCK(); mqtt_online=true; UNLOCK(); char subscription[100];

        snprintf(subscription,sizeof(subscription),"%s/command/#",settings.mqtt_prefix); esp_mqtt_client_subscribe(e->client,subscription,1);

        if(!sdm_meter() && !network_wallbox_meter()) { snprintf(subscription,sizeof(subscription),"%s/sensor/wallbox_power_w",settings.mqtt_prefix); esp_mqtt_client_subscribe(e->client,subscription,1); }

        snprintf(subscription,sizeof(subscription),"%s/input/+",settings.mqtt_prefix); esp_mqtt_client_subscribe(e->client,subscription,1);
        opendtu_subscribe(e->client);

        if(!settings.mqtt_state_json) { snprintf(subscription,sizeof(subscription),"%s/state",settings.mqtt_prefix); esp_mqtt_client_enqueue(e->client,subscription,"",0,1,1,false); }

        snprintf(subscription,sizeof(subscription),"%s/availability",settings.mqtt_prefix);

        esp_mqtt_client_enqueue(e->client,subscription,"online",0,1,1,false);
        homeassistant_discovery(settings.homeassistant_enabled);

    } else if(id==MQTT_EVENT_DISCONNECTED) {

        LOCK();if(settings.phase_output_transport==0&&phase_mqtt_seen_connection)phase_mqtt_cancel_locked(true);memset(&opendtu,0,sizeof(opendtu)); mqtt_online=false; if(!(settings.huawei_enabled && settings.huawei_battery)){battery_soc_at=0;battery_charge_at=0;battery_discharge_at=0;} car_soc_at=0; if(!(settings.huawei_enabled && settings.huawei_pv))pv_generation_at=0; if(!sdm_meter() && !network_wallbox_meter()) power_at=0; if(!settings.zero_feed_enabled) pv_at=0; UNLOCK(); expected=received=0;

    } else if(id==MQTT_EVENT_DATA) {

        if(e->current_data_offset==0) {

            received=expected=0;

            if(e->topic_len<=0 || e->topic_len>=(int)sizeof(topic) || e->total_data_len<=0 || e->total_data_len>=(int)sizeof(payload)) {

                LOCK(); mqtt_rejected++; UNLOCK(); return;

            }

            memcpy(topic,e->topic,e->topic_len); topic[e->topic_len]=0; expected=e->total_data_len;retained=e->retain;

        }

        if(!expected || e->current_data_offset!=received || e->data_len<0 || received+e->data_len>expected) { expected=0; return; }

        memcpy(payload+received,e->data,e->data_len); received+=e->data_len;

        if(received==expected) {

            payload[received]=0; expected=0; char prefix[80]; snprintf(prefix,sizeof(prefix),"%s/",settings.mqtt_prefix);

            bool ok=false;
            if(!memchr(payload,0,received)) {
                if(settings.mqtt_input_source==2 && opendtu_message(topic,payload,retained))ok=true;
                else if(!retained&&!strncmp(topic,prefix,strlen(prefix)))ok=mqtt_input(topic+strlen(prefix),payload);
            }

            if(!ok) { LOCK(); mqtt_rejected++; UNLOCK(); }

        }

    }

}

/* Called only by status_task, so reconnects cannot create duplicate clients. */

#include "phase_mqtt_network.inc"

static void start_mqtt(void) {

    if(!settings.mqtt_enabled || mqtt_client || !settings.mqtt_uri[0]) return;

    char will[100]; snprintf(will,sizeof(will),"%s/availability",settings.mqtt_prefix);

    esp_mqtt_client_config_t cfg={.broker.address.uri=settings.mqtt_uri,.broker.verification.crt_bundle_attach=esp_crt_bundle_attach,

        .credentials.username=settings.mqtt_username[0]?settings.mqtt_username:NULL,

        .credentials.authentication.password=settings.mqtt_password[0]?settings.mqtt_password:NULL,

        .session.last_will={.topic=will,.msg="offline",.qos=1,.retain=true},.session.keepalive=20,

        .network.timeout_ms=3000,.buffer.size=1024,.outbox.limit=16384};

    mqtt_client=esp_mqtt_client_init(&cfg);

    if(!mqtt_client) { ESP_LOGE(TAG,"MQTT allocation failed"); return; }

    esp_err_t err=esp_mqtt_client_register_event(mqtt_client,ESP_EVENT_ANY_ID,mqtt_event,NULL);

    if(err==ESP_OK) err=esp_mqtt_client_start(mqtt_client);

    if(err!=ESP_OK) { esp_mqtt_client_destroy(mqtt_client); mqtt_client=NULL; ESP_LOGE(TAG,"MQTT: %s",esp_err_to_name(err)); }

}

static esp_err_t send_json(httpd_req_t *req,cJSON *o) {

    if(!o) return httpd_resp_send_err(req,HTTPD_500_INTERNAL_SERVER_ERROR,"Out of memory");

    char *body=cJSON_PrintUnformatted(o); cJSON_Delete(o);

    if(!body) return httpd_resp_send_err(req,HTTPD_500_INTERNAL_SERVER_ERROR,"Out of memory");

    httpd_resp_set_type(req,"application/json; charset=utf-8"); httpd_resp_set_hdr(req,"Cache-Control","no-store");

    httpd_resp_set_hdr(req,"X-Content-Type-Options","nosniff"); esp_err_t err=httpd_resp_sendstr(req,body); free(body); return err;

}

static esp_err_t ok_json(httpd_req_t *req) { cJSON *o=cJSON_CreateObject(); cJSON_AddBoolToObject(o,"ok",true); return send_json(req,o); }

#include "http_json.inc"

static bool authorized(httpd_req_t *req) {

    char token[40];

    if(httpd_req_get_hdr_value_str(req,"X-EMS-Token",token,sizeof(token))!=ESP_OK || strcmp(token,csrf_token)) {

        http_json_error(req,HTTPD_403_FORBIDDEN,"Seite neu laden; Zugriffstoken fehlt."); return false;

    }

    LOCK(); bool reboot=restarting; UNLOCK();

    if(reboot) { httpd_resp_set_status(req,"503 Service Unavailable"); httpd_resp_sendstr(req,"Neustart laeuft."); return false; }

    LOCK();bool agreed=terms_accepted;UNLOCK();
    if(req->method==HTTP_POST && !agreed && strcmp(req->uri,"/api/terms")){
        httpd_resp_send_err(req,HTTPD_403_FORBIDDEN,"Nutzungsbedingungen zuerst bestaetigen.");return false;
    }
    return true;

}

static esp_err_t theme_get(httpd_req_t *req) {
    uint8_t selected=0;
    nvs_handle_t nvs;
    if(nvs_open("wallbox_ems",NVS_READONLY,&nvs)==ESP_OK) {
        if(nvs_get_u8(nvs,"ui_theme",&selected)!=ESP_OK) selected=0;
        nvs_close(nvs);
    }
    cJSON *o=cJSON_CreateObject();if(!o)return httpd_resp_send_err(req,HTTPD_500_INTERNAL_SERVER_ERROR,"Out of memory");
    cJSON_AddStringToObject(o,"theme",selected==2?"dark":selected==1?"light":"unset");
    return send_json(req,o);
}

static esp_err_t theme_post(httpd_req_t *req) {
    if(!authorized(req)) return ESP_OK;
    cJSON *o=read_json(req);if(!o)return ESP_OK;
    cJSON *theme=cJSON_GetObjectItemCaseSensitive(o,"theme");
    bool only=theme && theme==o->child && !theme->next && cJSON_IsString(theme);
    uint8_t selected=only&&!strcmp(theme->valuestring,"dark")?2:only&&!strcmp(theme->valuestring,"light")?1:0;
    cJSON_Delete(o);
    if(!selected)return httpd_resp_send_err(req,HTTPD_400_BAD_REQUEST,"Darstellung muss hell oder dunkel sein.");
    nvs_handle_t nvs;esp_err_t err=nvs_open("wallbox_ems",NVS_READWRITE,&nvs);
    if(err==ESP_OK){err=nvs_set_u8(nvs,"ui_theme",selected);if(err==ESP_OK)err=nvs_commit(nvs);nvs_close(nvs);}
    if(err!=ESP_OK)return httpd_resp_send_err(req,HTTPD_500_INTERNAL_SERVER_ERROR,"Darstellung konnte nicht gespeichert werden.");
    return theme_get(req);
}

static esp_err_t status_handler(httpd_req_t *req) { return send_json(req,status_json(true)); }

static esp_err_t control_handler(httpd_req_t *req) {

    if(!authorized(req)) return ESP_OK;

    cJSON *o=read_json(req); if(!o) return ESP_OK;

    bool ok=apply_control(o); cJSON_Delete(o);

    if(!ok) return httpd_resp_send_err(req,HTTPD_400_BAD_REQUEST,"Befehl abgelehnt. Modus, Ladeleistung, Akkuwerte und Neustartmeldung pruefen.");

    return ok_json(req);

}

typedef struct { const char *name; size_t offset,size; char type; } field_t;

#define FIELD(name,type) {#name,offsetof(settings_t,name),sizeof(((settings_t *)0)->name),type}

static const field_t fields[]={

    FIELD(shell_rs485_interface,'u'),FIELD(meter_rs485_interface,'u'),FIELD(house_rs485_interface,'u'),
    FIELD(shell_rs485_host,'s'),FIELD(meter_rs485_host,'s'),FIELD(house_rs485_host,'s'),FIELD(evcc_feature_enabled,'b'),
    FIELD(wifi_ssid,'s'),FIELD(wifi_password,'p'),FIELD(mqtt_uri,'s'),FIELD(mqtt_username,'s'),FIELD(mqtt_password,'p'),FIELD(mqtt_prefix,'s'),

    FIELD(grid_limit_a,'f'),FIELD(max_charge_a,'f'),FIELD(shell_limits_auto,'b'),FIELD(shell_setup_host,'s'),FIELD(xemex_address,'u'),FIELD(wallbox_tx_pin,'i'),FIELD(wallbox_rx_pin,'i'),


    FIELD(fixed_charge_phases,'u'),FIELD(nominal_v,'f'),FIELD(price_kwh,'f'),FIELD(solar_price_kwh,'f'),FIELD(control_verified,'b'),

    FIELD(zero_feed_enabled,'b'),FIELD(house_meter_type,'s'),FIELD(house_meter_host,'s'),FIELD(zero_reserve_w,'f'),FIELD(xemex_coils,'u'),FIELD(wallbox_meter_type,'s'),FIELD(meter_baud,'i'),FIELD(meter_format,'u'),FIELD(house_power_path,'s'),FIELD(mqtt_state_json,'b'),FIELD(battery_protect,'b'),FIELD(battery_reserve_soc,'f'),FIELD(battery_cloud_limit_w,'f'),FIELD(battery_assist_limit_w,'f'),FIELD(phase_switch_enabled,'b'),FIELD(phase_feedback_enabled,'b'),FIELD(phase_feedback_closed_is_single,'b'),
    FIELD(expert_mode,'b'),FIELD(basic_mode,'b'),FIELD(pv_allocation_enabled,'b'),FIELD(pv_priority,'u'),FIELD(pv_house_priority_w,'f'),FIELD(pv_car_priority_w,'f'),FIELD(control_status_visible,'b'),FIELD(vehicle_soc_enabled,'b'),FIELD(external_mode_input_enabled,'b'),FIELD(evu_input_enabled,'b'),
    FIELD(evu_limit_a,'f'),FIELD(grid_guard_enabled,'b'),
    FIELD(xemex_tx_pin,'i'),FIELD(xemex_rx_pin,'i'),FIELD(house_tx_pin,'i'),FIELD(house_rx_pin,'i'),
    FIELD(external_mode_input_pin,'i'),FIELD(evu_input_pin,'i'),FIELD(relay1_pin,'i'),
    FIELD(phase_feedback_pin,'i'),FIELD(phase_feedback_second_pin,'i'),FIELD(status_led_pin,'i'),
    FIELD(house_address,'u'),FIELD(house_meter_baud,'i'),FIELD(house_meter_format,'u'),
    FIELD(relay_board_enabled,'b'),FIELD(relay_active_low,'b'),
    FIELD(relay1_mode,'u'),
    FIELD(mqtt_enabled,'b'),FIELD(homeassistant_enabled,'b'),FIELD(pv_display_enabled,'b'),FIELD(mqtt_input_source,'u'),
    FIELD(phase_actuator_type,'u'),FIELD(phase_shelly_host,'s'),FIELD(phase_shelly_password,'p'),
    FIELD(phase_feedback_count,'u'),FIELD(phase_shelly_model,'u'),FIELD(phase_shelly_pulse,'b'),FIELD(phase_shelly_true_three_phase,'b'),
    FIELD(wallbox_meter_host,'s'),FIELD(charge_plan_enabled,'b'),FIELD(huawei_enabled,'b'),FIELD(huawei_host,'s'),FIELD(huawei_unit_id,'u'),FIELD(huawei_battery,'b'),FIELD(huawei_pv,'b'),FIELD(opendtu_prefix,'s'),FIELD(opendtu_pv_topic,'s'),FIELD(opendtu_pv_valid_topic,'s'),FIELD(opendtu_current_positive_discharge,'b')};

/* Read-only status metadata must not turn a settings backup into an invalid
   restore request or bypass the separate phase-output authorization. */
static void config_remove_readonly(cJSON *config) {
    for(cJSON *item=config->child,*next;item;item=next){
        next=item->next;bool editable=false;
        for(size_t f=0;f<sizeof(fields)/sizeof(fields[0]);f++)
            if(!strcmp(item->string,fields[f].name)){editable=true;break;}
        if(!editable)cJSON_Delete(cJSON_DetachItemViaPointer(config,item));
    }
}
static cJSON *config_json(bool secrets) {

    cJSON *o=cJSON_CreateObject(); if(!o) return NULL; LOCK();

    for(size_t f=0;f<sizeof(fields)/sizeof(fields[0]);f++) {

        const field_t *d=&fields[f]; const void *p=(const char *)&saved_settings+d->offset;

        if(d->type=='s'||(secrets && d->type=='p')) cJSON_AddStringToObject(o,d->name,p);

        else if(d->type=='f') cJSON_AddNumberToObject(o,d->name,*(const float *)p);

        else if(d->type=='i') cJSON_AddNumberToObject(o,d->name,*(const int *)p);

        else if(d->type=='u') cJSON_AddNumberToObject(o,d->name,*(const uint8_t *)p);

        else if(d->type=='b') cJSON_AddBoolToObject(o,d->name,*(const bool *)p);

    }

    cJSON_AddBoolToObject(o,"wifi_password_set",saved_settings.wifi_password[0]!=0);

    cJSON_AddBoolToObject(o,"mqtt_password_set",saved_settings.mqtt_password[0]!=0);
    cJSON_AddBoolToObject(o,"phase_shelly_password_set",saved_settings.phase_shelly_password[0]!=0);
    cJSON_AddStringToObject(o,"phase_mqtt_topic",saved_settings.phase_mqtt_topic);
    cJSON_AddBoolToObject(o,"phase_mqtt_output_enabled",saved_settings.phase_mqtt_output_enabled);
    cJSON_AddBoolToObject(o,"phase_mqtt_true_three_phase",saved_settings.phase_mqtt_true_three_phase);
    cJSON_AddNumberToObject(o,"phase_output_transport",saved_settings.phase_output_transport);
    cJSON_AddNumberToObject(o,"phase_output_switch_id",saved_settings.phase_output_switch_id);
    cJSON_AddStringToObject(o,"phase_output_host",saved_settings.phase_output_host);
    cJSON_AddBoolToObject(o,"phase_output_password_set",saved_settings.phase_output_password[0]!=0);


    UNLOCK(); return o;

}

static esp_err_t config_apply(httpd_req_t *req,cJSON *o,bool restore) {
    if(!cJSON_IsObject(o)){
        cJSON_Delete(o);
        return httpd_resp_send_err(req,HTTPD_400_BAD_REQUEST,"Fehlerhafte Einstellungsdaten. Seite neu laden und erneut speichern.");
    }
    if(cJSON_HasObjectItem(o,"phase_mqtt_output_enabled")||cJSON_HasObjectItem(o,"phase_mqtt_topic")||
       cJSON_HasObjectItem(o,"phase_mqtt_true_three_phase")||cJSON_HasObjectItem(o,"phase_output_transport")||
       cJSON_HasObjectItem(o,"phase_output_host")||cJSON_HasObjectItem(o,"phase_output_switch_id")||
       cJSON_HasObjectItem(o,"phase_output_password")){
        cJSON_Delete(o);return httpd_resp_send_err(req,HTTPD_403_FORBIDDEN,"Ohne Rueckmeldung im Expertenmodus gesondert freigeben.");
    }
    for(unsigned role=2;role<EMS_GPIO_ROLES&&!restore;role++){
        cJSON *pin=cJSON_GetObjectItemCaseSensitive(o,gpio_roles[role].id);
        if(pin&&(!cJSON_IsNumber(pin)||pin->valuedouble!=settings_gpio_value(&saved_settings,role))){
            cJSON_Delete(o);return httpd_resp_send_err(req,HTTPD_403_FORBIDDEN,"GPIO-Belegung im Expertenmodus aendern.");
        }
    }
    cJSON *mqtt_profile=cJSON_GetObjectItemCaseSensitive(o,"phase_actuator_type");
    if(cJSON_IsNumber(mqtt_profile)&&mqtt_profile->valuedouble==2&&saved_settings.phase_actuator_type!=2){
        cJSON_Delete(o);return httpd_resp_send_err(req,HTTPD_403_FORBIDDEN,"MQTT-Schuetzsteuerung erfordert Warnbestaetigungen und erneutes Experten-Passwort.");
    }
    LOCK();bool maintenance=phase_test_active||boot_validation_pending||ota_in_progress||restarting||evcc_configuring||(evcc_control.configured&&evcc_control.enabled);UNLOCK();
    if(maintenance){cJSON_Delete(o);httpd_resp_set_status(req,"409 Conflict");return httpd_resp_sendstr(req,"Sicherung oder Update laeuft. Danach erneut versuchen.");}


    settings_t next; LOCK(); next=saved_settings; UNLOCK(); bool valid=true;
    char invalid_field[64]={0};

    for(cJSON *i=o->child;i && valid;i=i->next) {
        snprintf(invalid_field,sizeof(invalid_field),"%s",i->string?i->string:"unbekannt");
        if(!i->string){valid=false;break;}
        /* Reject ambiguous duplicate fields before applying their values. */
        for(cJSON *previous=o->child;previous!=i;previous=previous->next)
            if(previous->string&&!strcmp(previous->string,i->string)){valid=false;break;}
        if(!valid)break;

        const field_t *d=NULL;

        for(size_t f=0;f<sizeof(fields)/sizeof(fields[0]);f++) if(!strcmp(i->string,fields[f].name)) d=&fields[f];

        if(!d) { valid=false; break; }

        void *p=(char *)&next+d->offset;

        if(d->type=='s' || d->type=='p') { valid=cJSON_IsString(i) && strlen(i->valuestring)<d->size; if(valid) strcpy(p,i->valuestring); }

        else if(d->type=='b') { valid=cJSON_IsBool(i); if(valid) *(bool *)p=cJSON_IsTrue(i); }

        else {

            double v=i->valuedouble; valid=cJSON_IsNumber(i) && isfinite(v) && v>=-5000 && v<=38400;

            if(valid && d->type=='f') *(float *)p=v;

            else if(valid) {

                valid=floor(v)==v && (d->type=='i' ? (v>=INT_MIN && v<=INT_MAX) : (v>=0 && v<=255));

                if(valid && d->type=='i') *(int *)p=(int)v;

                if(valid && d->type=='u') *(uint8_t *)p=(uint8_t)v;

            }

        }

    }

    cJSON_Delete(o);
    /* A partial request must never be validated as if it were a complete
       configuration: later host/coils may not have been parsed at all. */
    if(!valid){
        char message[192];
        snprintf(message,sizeof(message),"Fehlerhafte Einstellungsdaten (%s). Seite neu laden und erneut speichern.",invalid_field);
        return httpd_resp_send_err(req,HTTPD_400_BAD_REQUEST,message);
    }
    /* These legacy values are intentionally no longer configurable. */
    next.wallbox_rts_pin=-1;
    next.xemex_rts_pin=-1;
    next.house_rts_pin=-1;
    next.power_factor=1;
    next.wallbox_address=1;next.house_xemex_coils=3;
    settings_fixed_pins(&next);
    settings_basic_mode(&next);
    if(!settings_shell_pin_available(&next,next.wallbox_tx_pin,true) ||
       !settings_shell_pin_available(&next,next.wallbox_rx_pin,false))
        return httpd_resp_send_err(req,HTTPD_400_BAD_REQUEST,"Shell-Pins: freie, geeignete GPIOs fuer TX und RX waehlen; keine Doppelbelegung.");
    /* The legacy combined /state topic was removed from the user interface. */
    next.mqtt_state_json=false;
    if(!next.relay_board_enabled) next.phase_switch_enabled=false;
    /* Home Assistant discovery follows MQTT automatically. The former switch
       was misleading because retained entities may remain visible in HA. */
    next.homeassistant_enabled=next.mqtt_enabled;
    if(next.mqtt_input_source==1)next.mqtt_input_source=0;
    if(!next.mqtt_enabled){next.vehicle_soc_enabled=false;
        if(!(next.huawei_enabled && next.huawei_pv))next.pv_display_enabled=false;
        if(!(next.huawei_enabled && next.huawei_battery))next.battery_protect=false;}
    if(next.phase_actuator_type!=2)next.phase_feedback_enabled=true;
    next.relay1_mode=next.phase_switch_enabled?3:0;
    if(next.phase_actuator_type==2)next.phase_feedback_enabled=false;
    if(next.phase_actuator_type!=2||!next.phase_switch_enabled||!next.relay_board_enabled||
       (next.phase_output_transport==0&&!next.mqtt_enabled))
        next.phase_mqtt_output_enabled=false;
    next.enabled=false; next.mode=MODE_OFF; next.pv_surplus_a=0;
    if(restore)next.control_verified=false;
    /* Clear the retired Solis experiment while retaining its settings-byte slot. */
    next.reserved_solis_discharge=false;
    next.reserved_http_meter_enabled=false;next.reserved_http_meter_host[0]=0;next.reserved_feedback_source=0;
    next.house_meter_password[0]=0;next.wallbox_meter_password[0]=0;

    /* A different physical meter invalidates the previous commissioning check. */

    bool meter_changed=next.shell_rs485_interface!=saved_settings.shell_rs485_interface ||
        next.meter_rs485_interface!=saved_settings.meter_rs485_interface ||
        strcmp(next.shell_rs485_host,saved_settings.shell_rs485_host) ||
        strcmp(next.meter_rs485_host,saved_settings.meter_rs485_host) || strcmp(next.wallbox_meter_type,saved_settings.wallbox_meter_type) ||
        strcmp(next.wallbox_meter_host,saved_settings.wallbox_meter_host) ||
        next.xemex_coils!=saved_settings.xemex_coils || next.meter_baud!=saved_settings.meter_baud ||
        next.meter_format!=saved_settings.meter_format || next.xemex_address!=saved_settings.xemex_address ||
        next.wallbox_tx_pin!=saved_settings.wallbox_tx_pin || next.wallbox_rx_pin!=saved_settings.wallbox_rx_pin ||
        next.xemex_tx_pin!=saved_settings.xemex_tx_pin || next.xemex_rx_pin!=saved_settings.xemex_rx_pin;

    if(meter_changed) { next.control_verified=false;next.current_offset_a=.8f; }

    next.manual_current_a=fminf(next.manual_current_a,next.max_charge_a);

    LOCK(); bool phase_change_allowed=phase_setup_change_allowed(
        saved_settings.phase_switch_enabled,next.phase_switch_enabled,
        settings.charge_phases,phase_config_quiescent_locked(),
        settings.enabled||(evcc_control.configured&&evcc_control.enabled&&!evcc_control.inhibited),
        actual_at>0||meter_at>0,
        fresh(now_ms(),actual_at,wallbox_meter_ttl()),actual_a); UNLOCK();
    LOCK();bool fixed_change_allowed=fixed_wiring_change_allowed(
        saved_settings.fixed_charge_phases,next.fixed_charge_phases,
        settings.enabled||(evcc_control.configured&&evcc_control.enabled&&!evcc_control.inhibited),
        phase_config_quiescent_locked(),actual_at>0||meter_at>0,
        fresh(now_ms(),actual_at,wallbox_meter_ttl()),actual_a);UNLOCK();
    LOCK();bool actuator_change=next.phase_actuator_type!=saved_settings.phase_actuator_type ||
        strcmp(next.phase_shelly_host,saved_settings.phase_shelly_host) ||
        next.phase_shelly_model!=saved_settings.phase_shelly_model||next.phase_shelly_pulse!=saved_settings.phase_shelly_pulse||
        next.phase_shelly_true_three_phase!=saved_settings.phase_shelly_true_three_phase||
        strcmp(next.phase_shelly_password,saved_settings.phase_shelly_password);
    config_gate_t hardware_gate=phase_config_gate_locked(now_ms());
    bool hardware_change_allowed=hardware_gate==CONFIG_GATE_OK;
    bool actuator_change_allowed=!actuator_change||hardware_change_allowed;UNLOCK();
    if(!actuator_change_allowed)return httpd_resp_send_err(req,HTTPD_400_BAD_REQUEST,phase_config_gate_message(hardware_gate));
    LOCK();bool feedback_changed=next.phase_feedback_enabled!=saved_settings.phase_feedback_enabled ||
        next.phase_feedback_count!=saved_settings.phase_feedback_count||
        next.phase_feedback_closed_is_single!=saved_settings.phase_feedback_closed_is_single;
    bool feedback_change_allowed=!feedback_changed||hardware_change_allowed;
    bool phase_configuration_changed=actuator_change||feedback_changed||
        next.phase_switch_enabled!=saved_settings.phase_switch_enabled||
        next.fixed_charge_phases!=saved_settings.fixed_charge_phases;
    UNLOCK();
    if(!feedback_change_allowed)return httpd_resp_send_err(req,HTTPD_400_BAD_REQUEST,phase_config_gate_message(hardware_gate));
    if(!fixed_change_allowed)return httpd_resp_send_err(req,HTTPD_400_BAD_REQUEST,phase_config_gate_message(hardware_gate));
    if(!phase_change_allowed)return httpd_resp_send_err(req,HTTPD_400_BAD_REQUEST,phase_config_gate_message(hardware_gate));
    char house_url[256];

    bool next_tasmota_house=!strcmp(next.house_meter_type,"tasmota");
    bool next_three_phase_house=(!strcmp(next.house_meter_type,"xemex") && next.house_xemex_coils==3) ||
        !strcmp(next.house_meter_type,"sdm630") || !strcmp(next.house_meter_type,"sdm630mct") ||
        !strcmp(next.house_meter_type,"shelly_gen2") || !strcmp(next.house_meter_type,"em24_tcp") ||
        (!strcmp(next.house_meter_type,"huawei") && next.huawei_enabled);
    if(next.battery_protect && (!(next.mqtt_enabled || (next.huawei_enabled && next.huawei_battery)) || !next.zero_feed_enabled || next.zero_reserve_w>0))
        return httpd_resp_send_err(req,HTTPD_400_BAD_REQUEST,
            "Hausakku benoetigt eine Datenquelle, einen PV-Hauszaehler und ein Netzziel von hoechstens 0 W.");
    if(next.phase_switch_enabled && (!next.relay_board_enabled || next.relay1_mode!=3))
        return httpd_resp_send_err(req,HTTPD_400_BAD_REQUEST,
            "Phasenumschaltung ist nicht vollstaendig eingerichtet. Verbindung und Schuetzoption pruefen.");
    if((next.phase_actuator_type==1||next.phase_actuator_type==3)&&next.phase_switch_enabled){
        if(!next.phase_shelly_host[0])
            return httpd_resp_send_err(req,HTTPD_400_BAD_REQUEST,"Shelly-Adresse fehlt. Unter Shelly-Schaltgeraet eintragen.");
        if(!phase_switch_meter_supported(&next)){
            char message[224];
            if(!strcmp(next.wallbox_meter_type,"xemex"))
                snprintf(message,sizeof(message),"Wallbox-Zaehler: Xemex mit %u Messspule(n) eingestellt. Fuer die Shelly-Phasenumschaltung mindestens L1 und L2 messen.",(unsigned)next.xemex_coils);
            else
                snprintf(message,sizeof(message),"Wallbox-Zaehlerprofil '%s' erfasst nicht mindestens L1 und L2. Fuer die Shelly-Phasenumschaltung ein passendes Profil waehlen.",next.wallbox_meter_type);
            return httpd_resp_send_err(req,HTTPD_400_BAD_REQUEST,message);
        }
    }
    if(next.grid_guard_enabled && !next_three_phase_house)
        return httpd_resp_send_err(req,HTTPD_400_BAD_REQUEST,
            "Hausanschlussschutz benoetigt einen Zaehler mit drei gemessenen Phasenstroemen.");
    if(!settings_valid(&next) || (next.zero_feed_enabled && next_tasmota_house &&
       !house_query_url(next.house_meter_type,next.house_meter_host,next.house_power_path,house_url,sizeof(house_url))))
        return httpd_resp_send_err(req,HTTPD_400_BAD_REQUEST,"Einstellungen ungueltig. Pins, Zaehler, Ladegrenzen und WLAN-Passwort pruefen.");

    /* Recheck under lock and reserve the controller before flash I/O. A
       worker may have started since validation; never race its command. */
    LOCK();hardware_gate=phase_config_gate_locked(now_ms());
    if(phase_configuration_changed&&hardware_gate!=CONFIG_GATE_OK){UNLOCK();return httpd_resp_send_err(req,HTTPD_400_BAD_REQUEST,phase_config_gate_message(hardware_gate));}
    phase_config_saving=phase_configuration_changed;UNLOCK();
    if(save_config(&next)!=ESP_OK){
        LOCK();phase_config_saving=false;UNLOCK();
        return httpd_resp_send_err(req,HTTPD_500_INTERNAL_SERVER_ERROR,"Speichern fehlgeschlagen.");
    }
    if(meter_changed) {
        nvs_handle_t nvs;if(nvs_open("wallbox_ems",NVS_READWRITE,&nvs)==ESP_OK){
            nvs_erase_key(nvs,"auto_offset");nvs_commit(nvs);nvs_close(nvs);
        }
    }

    LOCK();
    float previous_reserve=settings.battery_reserve_soc;
    float previous_cloud_limit=settings.battery_cloud_limit_w;
    float previous_assist_limit=settings.battery_assist_limit_w;
    bool previous_battery_protect=settings.battery_protect;
    bool allocation_changed=settings.pv_allocation_enabled!=next.pv_allocation_enabled || settings.pv_priority!=next.pv_priority ||
        settings.pv_house_priority_w!=next.pv_house_priority_w || settings.pv_car_priority_w!=next.pv_car_priority_w;
    bool previous_vehicle_soc=settings.vehicle_soc_enabled;
    bool previous_homeassistant=settings.homeassistant_enabled;
    bool pv_source_changed=settings.zero_feed_enabled!=next.zero_feed_enabled || settings.zero_reserve_w!=next.zero_reserve_w;
    bool huawei_connection_changed=settings.huawei_enabled!=next.huawei_enabled ||
        settings.huawei_unit_id!=next.huawei_unit_id || strcmp(settings.huawei_host,next.huawei_host);
    bool huawei_battery_changed=huawei_connection_changed || settings.huawei_battery!=next.huawei_battery;
    bool huawei_pv_changed=huawei_connection_changed || settings.huawei_pv!=next.huawei_pv;
    bool shell_source_changed=settings.shell_limits_auto!=next.shell_limits_auto || strcmp(settings.shell_setup_host,next.shell_setup_host);
    bool manual_limits_changed=saved_settings.max_charge_a!=next.max_charge_a || saved_settings.grid_limit_a!=next.grid_limit_a;
    bool house_network_changed=strcmp(settings.house_meter_host,next.house_meter_host) || strcmp(settings.house_power_path,next.house_power_path);
    if(next.price_kwh!=settings.price_kwh || next.solar_price_kwh!=settings.solar_price_kwh) {
        int64_t epoch,midnight,at=now_ms();uint32_t date=calendar(&epoch,&midnight);
        /* Close the preceding interval at its previous price, then apply the
           new tariff from this exact boundary, including within one session. */
        sessions_step(&sessions,at,epoch,date,estimated_charge_power(&settings,meter_a),
            meter_at?meter_at+wallbox_meter_ttl():0,house_power_w,house_power_at?house_power_at+house_meter_ttl():0,
            next.price_kwh,next.solar_price_kwh);
        sessions.checkpoint=true;
    }
    bool applied=settings_apply_live(&settings,&saved_settings,&next);
    if(applied) {
        if(meter_changed){wallbox_meter_revision++;meter_at=actual_at=power_at=0;}
        if(house_network_changed){house_meter_revision++;house_power_at=house_current_at=pv_at=0;}
        if(manual_limits_changed){memset(&manual_filter,0,sizeof(manual_filter));memset(&current_calibration,0,sizeof(current_calibration));}
        if(shell_source_changed){shell_active_host[0]=0;shell_limits_at=0;evcc_poll_generation++;}
        /* Measurements belong to a source. A new source must supply its own
           fresh reading before it participates in PV/battery regulation. */
        if(huawei_battery_changed)battery_soc_at=battery_charge_at=battery_discharge_at=0;
        if(huawei_pv_changed)pv_generation_at=0;
        if(huawei_connection_changed && !strcmp(settings.house_meter_type,"huawei"))house_power_at=house_current_at=pv_at=0;
        if(!settings.zero_feed_enabled && settings.mode==MODE_PV){settings.enabled=false;settings.mode=MODE_OFF;}
    }
    if(!next.evcc_feature_enabled && evcc_control.configured){evcc_configure(&evcc_control,false);settings.enabled=false;settings.mode=MODE_OFF;}
    /* Prices take effect even when another changed field needs a restart. */
    settings.price_kwh=next.price_kwh;settings.solar_price_kwh=next.solar_price_kwh;
    if(meter_changed){memset(&current_calibration,0,sizeof(current_calibration));calibration_dirty=false;}
    if(settings.phase_actuator_type==2&&!next.phase_mqtt_output_enabled){phase_mqtt_allowed=false;phase_mqtt_cancel_locked(true);}
    saved_settings=next;
    settings.charge_plan_enabled=next.charge_plan_enabled;
    if(!next.charge_plan_enabled && charge_plan.active){
        charge_plan.active=false;plan_phase=PLAN_OFF;plan_dirty=true;plan_revision++;
        settings.enabled=false;settings.mode=MODE_OFF;event_locked("plan_cancelled");
    }
    if(restore){settings.enabled=false;settings.mode=MODE_OFF;settings.control_verified=false;
        charge_plan.active=false;plan_phase=PLAN_OFF;plan_dirty=true;plan_revision++;reboot_required=true;}
    event_locked(restore?"settings_restored":"settings_saved");
    if(applied && !settings.battery_protect) {
        battery_use=false; battery_start_use=false; memset(&battery_buffer,0,sizeof(battery_buffer));
    }
    if(applied && previous_vehicle_soc && !settings.vehicle_soc_enabled) car_soc_at=0;
    if(applied && (house_network_changed || pv_source_changed || huawei_battery_changed || allocation_changed || previous_reserve!=settings.battery_reserve_soc || previous_battery_protect!=settings.battery_protect ||
                   previous_cloud_limit!=settings.battery_cloud_limit_w || previous_assist_limit!=settings.battery_assist_limit_w) && settings.mode==MODE_PV) {
        int64_t now=now_ms();
        if(settings.zero_feed_enabled && fresh(now,house_power_at,house_meter_ttl()) &&
           fresh(now,actual_at,wallbox_meter_ttl())) {
            settings.pv_surplus_a=pv_available_current_locked(now,house_power_w);
            pv_at=now;
            /* Preference changes use the normal ramp and deficit grace period.
               A true SOC/stale-input block is still checked every second. */
        } else {
            settings.pv_surplus_a=0; pv_at=0;
            pv_control_step(&pv_control,false,0,active_min_a(),settings.max_charge_a,now);
        }
    }
    if(!applied) { reboot_required=true; settings.enabled=false; }
    bool needs_reboot=restart_pending();
    phase_config_saving=false;
    UNLOCK();
    if(mqtt_client && previous_homeassistant && !next.homeassistant_enabled)
        homeassistant_discovery(false);
    else if(mqtt_client && applied && !previous_homeassistant && next.homeassistant_enabled)
        homeassistant_discovery(true);
    cJSON *result=cJSON_CreateObject(); if(!result) return send_json(req,result);
    cJSON_AddBoolToObject(result,"ok",true);
    cJSON_AddBoolToObject(result,"reboot_required",needs_reboot);
    cJSON_AddBoolToObject(result,"applied",applied);
    cJSON_AddBoolToObject(result,"restart_needed",!applied||restore);
    return send_json(req,result);

}

#include "expert_http.inc"
#include "phase_test_http.inc"
#include "feature_http.inc"

static esp_err_t time_handler(httpd_req_t *req) {

    if(!authorized(req)) return ESP_OK;

    cJSON *o=read_json(req); if(!o) return ESP_OK;

    cJSON *i=cJSON_GetObjectItemCaseSensitive(o,"epoch");

    bool ok=cJSON_IsNumber(i) && isfinite(i->valuedouble) && i->valuedouble>=1704067200 && i->valuedouble<4102444800LL;

    if(ok) { struct timeval tv={.tv_sec=(time_t)i->valuedouble}; ok=settimeofday(&tv,NULL)==0; }

    cJSON_Delete(o); if(!ok) return httpd_resp_send_err(req,HTTPD_400_BAD_REQUEST,"Uhrzeit ungueltig."); return ok_json(req);

}

static esp_err_t energy_reset_handler(httpd_req_t *req) {
    if(!authorized(req)) return ESP_OK;
    int64_t epoch,minute;uint32_t date=calendar(&epoch,&minute);
    if(!date){httpd_resp_set_status(req,"503 Service Unavailable");return httpd_resp_sendstr(req,"Internetzeit noch nicht verfuegbar.");}
    double previous[EMS_SOURCES];uint32_t previous_date;
    LOCK();memcpy(previous,energy_reset_wh,sizeof(previous));previous_date=energy_reset_date;
    for(int i=0;i<EMS_SOURCES;i++)energy_reset_wh[i]=energy.store.total_wh[i];
    energy_reset_date=date;UNLOCK();
    if(!save_energy()){
        LOCK();memcpy(energy_reset_wh,previous,sizeof(previous));energy_reset_date=previous_date;UNLOCK();
        return httpd_resp_send_err(req,HTTPD_500_INTERNAL_SERVER_ERROR,"Zaehlerstand konnte nicht gespeichert werden.");
    }
    return ok_json(req);
}

static esp_err_t sessions_reset_handler(httpd_req_t *req) {
    if(!authorized(req)) return ESP_OK;
    sessions_t *previous=malloc(sizeof(*previous));
    if(!previous)return httpd_resp_send_err(req,HTTPD_500_INTERNAL_SERVER_ERROR,"Nicht genug Speicher.");
    LOCK();*previous=sessions;sessions_init(&sessions);UNLOCK();
    if(!save_energy()){
        LOCK();sessions=*previous;UNLOCK();free(previous);
        return httpd_resp_send_err(req,HTTPD_500_INTERNAL_SERVER_ERROR,"Ladesitzungen konnten nicht geloescht werden.");
    }
    free(previous);return ok_json(req);
}

static esp_err_t reboot_handler(httpd_req_t *req) {

    if(!authorized(req)) return ESP_OK;

    LOCK();bool allowed=!ota_in_progress&&!backup_in_progress&&!evcc_configuring;
    if(allowed){settings.enabled=false;evcc_local_stop(&evcc_control);phase_test_end_locked();restarting=true;}UNLOCK();
    if(!allowed){httpd_resp_set_status(req,"409 Conflict");return httpd_resp_sendstr(req,"Wartung zuerst abschliessen. Neustart noch nicht ausgefuehrt.");}
    return ok_json(req);

}

static esp_err_t factory_handler(httpd_req_t *req) {

    if(!authorized(req)) return ESP_OK;

    LOCK();bool allowed=!ota_in_progress&&!backup_in_progress&&!evcc_configuring;
    if(allowed){settings.enabled=false;evcc_local_stop(&evcc_control);phase_test_end_locked();restarting=true;factory_reset_requested=true;}UNLOCK();
    if(!allowed){httpd_resp_set_status(req,"409 Conflict");return httpd_resp_sendstr(req,"Wartung zuerst abschliessen. Werksreset noch nicht ausgefuehrt.");}

    return ok_json(req);

}

static esp_err_t ap_handler(httpd_req_t *req) {

    if(!authorized(req)) return ESP_OK;

    LOCK(); ap_requested=true; UNLOCK(); return ok_json(req);

}

#include "ota_http.inc"
#include "github_update.inc"
#include "system_backup.inc"

static esp_err_t wifi_scan_handler(httpd_req_t *req) {

    if(xSemaphoreTake(wifi_action_lock,ticks(100))!=pdTRUE) { httpd_resp_set_status(req,"503 Service Unavailable"); return httpd_resp_sendstr(req,"WLAN wird gerade umgeschaltet. Bitte erneut versuchen."); }

    LOCK(); wifi_scanning=true; UNLOCK();

    wifi_mode_t previous=WIFI_MODE_AP; esp_wifi_get_mode(&previous);

    if(previous==WIFI_MODE_AP) esp_wifi_set_mode(WIFI_MODE_APSTA);

    wifi_scan_config_t scan={.scan_type=WIFI_SCAN_TYPE_ACTIVE,.scan_time.active={.min=80,.max=250}};

    esp_err_t err=esp_wifi_scan_start(&scan,true);

    if(err!=ESP_OK) { if(previous==WIFI_MODE_AP) esp_wifi_set_mode(previous); LOCK(); wifi_scanning=false; UNLOCK(); xSemaphoreGive(wifi_action_lock); httpd_resp_set_status(req,"503 Service Unavailable"); return httpd_resp_sendstr(req,"WLAN-Suche nicht verfuegbar"); }

    uint16_t count=20; wifi_ap_record_t records[20]; memset(records,0,sizeof(records));

    if(esp_wifi_scan_get_ap_records(&count,records)!=ESP_OK) { if(previous==WIFI_MODE_AP) esp_wifi_set_mode(previous); LOCK(); wifi_scanning=false; UNLOCK(); xSemaphoreGive(wifi_action_lock); return httpd_resp_send_err(req,HTTPD_500_INTERNAL_SERVER_ERROR,"WLAN-Suche fehlgeschlagen"); }

    cJSON *o=cJSON_CreateObject(),*a=cJSON_AddArrayToObject(o,"networks");

    for(uint16_t i=0;i<count;i++) if(records[i].ssid[0]) { cJSON *n=cJSON_CreateObject(); cJSON_AddStringToObject(n,"ssid",(char *)records[i].ssid); cJSON_AddNumberToObject(n,"rssi",records[i].rssi); cJSON_AddItemToArray(a,n); }

    if(previous==WIFI_MODE_AP) esp_wifi_set_mode(previous);

    LOCK(); wifi_scanning=false; UNLOCK();

    xSemaphoreGive(wifi_action_lock);

    return send_json(req,o);

}

static int compare_days(const void *a,const void *b) {

    uint32_t da=((const energy_day_t *)a)->date,db=((const energy_day_t *)b)->date; return (da>db)-(da<db);

}

static esp_err_t history_handler(httpd_req_t *req) {

    cJSON *o=cJSON_CreateObject(); if(!o) return send_json(req,o);

    cJSON *points=cJSON_AddArrayToObject(o,"points"),*days=cJSON_AddArrayToObject(o,"days");

    energy_t *copy=malloc(sizeof(*copy)); if(!copy) { cJSON_Delete(o); return send_json(req,NULL); }

    LOCK(); *copy=energy; UNLOCK(); int64_t last=now_ms()/60000;

    for(int64_t m=last-EMS_HISTORY+1;m<=last;m++) {

        if(m<0) continue;

        energy_point_t *p=&copy->history[m%EMS_HISTORY]; cJSON *item=cJSON_CreateObject(); cJSON_AddNumberToObject(item,"age_min",last-m);

        for(int s=0;s<2;s++) nullable(item,s?"wallbox_w":"estimate_w",p->covered_ms[s]?p->wh[s]*3600000/p->covered_ms[s]:0,p->minute==m && p->covered_ms[s]>0);

        cJSON_AddItemToArray(points,item);

    }

    qsort(copy->store.days,EMS_DAYS,sizeof(energy_day_t),compare_days);

    for(int d=0;d<EMS_DAYS;d++) {

        energy_day_t *day=&copy->store.days[d]; if(!day->date) continue;

        cJSON *item=cJSON_CreateObject(); cJSON_AddNumberToObject(item,"date",day->date);

        for(int s=0;s<2;s++) {

            nullable(item,s?"wallbox_kwh":"estimate_kwh",day->wh[s]/1000,day->covered_ms[s]>0);

            cJSON_AddNumberToObject(item,s?"wallbox_covered_s":"estimate_covered_s",day->covered_ms[s]/1000);

        }

        cJSON_AddItemToArray(days,item);

    }

    free(copy); return send_json(req,o);

}

typedef struct{uint32_t period,from_date,to_date;bool custom;} session_filter_t;
static bool session_date_parse(const char *text,uint32_t *date){
    if(!text||!date||strlen(text)!=10||text[4]!='-'||text[7]!='-')return false;
    unsigned value=0;for(unsigned i=0;i<10;i++){if(i==4||i==7)continue;if(text[i]<'0'||text[i]>'9')return false;value=value*10+(unsigned)(text[i]-'0');}
    unsigned year=value/10000,month=value/100%100,day=value%100;
    if(year<2024||year>2100||month<1||month>12||day<1||day>31)return false;
    *date=value;return true;
}
static bool session_filter_matches(const charge_session_t *r,const session_filter_t *filter){
    return filter->custom?r->start_date>=filter->from_date&&r->start_date<=filter->to_date:session_matches(r,filter->period);
}
static bool session_query(httpd_req_t *req,session_filter_t *filter,unsigned *offset,char label[32]) {
    char query[192]={0},text[16]={0},from[16]={0},to[16]={0};*offset=0;memset(filter,0,sizeof(*filter));
    if(httpd_req_get_url_query_len(req)>=sizeof(query))return false;
    if(httpd_req_get_url_query_str(req,query,sizeof(query))==ESP_OK){
        bool has_from=httpd_query_key_value(query,"from",from,sizeof(from))==ESP_OK;
        bool has_to=httpd_query_key_value(query,"to",to,sizeof(to))==ESP_OK;
        if(has_from||has_to){if(!has_from||!has_to||!session_date_parse(from,&filter->from_date)||!session_date_parse(to,&filter->to_date)||filter->from_date>filter->to_date)return false;filter->custom=true;}
        else if(httpd_query_key_value(query,"period",text,sizeof(text))==ESP_OK){if(!session_period_parse(text,&filter->period))return false;}
        else{int64_t e,m;uint32_t date=calendar(&e,&m);if(!date)return false;filter->period=date/100;}
        if(httpd_query_key_value(query,"offset",text,sizeof(text))==ESP_OK){double v;if(!parse_number(text,0,CHARGE_SESSION_CAPACITY,&v)||v!=floor(v))return false;*offset=(unsigned)v;}
    }else{int64_t e,m;uint32_t date=calendar(&e,&m);if(!date)return false;filter->period=date/100;}
    if(filter->custom)snprintf(label,32,"%08lu-bis-%08lu",(unsigned long)filter->from_date,(unsigned long)filter->to_date);
    else if(filter->period<10000)snprintf(label,32,"%04lu",(unsigned long)filter->period);
    else snprintf(label,32,"%04lu-%02lu",(unsigned long)filter->period/100,(unsigned long)filter->period%100);
    return true;
}
typedef struct {
    uint32_t date,flags;
    unsigned sessions;
    uint64_t charging_ms;
    double grid_wh,solar_wh,unknown_wh,cost_eur,grid_cost_eur,solar_cost_eur;
    bool active;
} session_day_t;
static void session_day_add(session_day_t *day,const charge_session_t *r,bool active){
    day->date=r->start_date;day->sessions++;day->charging_ms+=r->charging_ms;
    day->grid_wh+=r->grid_wh+r->unknown_wh;day->solar_wh+=r->solar_wh;day->cost_eur+=r->cost_eur;
    day->grid_cost_eur+=r->grid_cost_eur;day->solar_cost_eur+=r->solar_cost_eur;
    day->flags|=r->flags;day->active|=active;
}
static void session_day_emit(cJSON *rows,const session_day_t *day,unsigned offset,unsigned *count,unsigned *shown){
    if((*count)++<offset||*shown>=20)return;
    cJSON *item=cJSON_CreateObject();if(!item)return;
    cJSON_AddNumberToObject(item,"date",day->date);cJSON_AddNumberToObject(item,"sessions",day->sessions);
    cJSON_AddNumberToObject(item,"charging_s",day->charging_ms/1000);
    cJSON_AddNumberToObject(item,"energy_kwh",(day->grid_wh+day->solar_wh+day->unknown_wh)/1000);
    cJSON_AddNumberToObject(item,"grid_kwh",day->grid_wh/1000);cJSON_AddNumberToObject(item,"solar_kwh",day->solar_wh/1000);
    cJSON_AddNumberToObject(item,"unknown_kwh",day->unknown_wh/1000);cJSON_AddNumberToObject(item,"cost_eur",day->cost_eur);
    cJSON_AddNumberToObject(item,"grid_cost_eur",day->grid_cost_eur);cJSON_AddNumberToObject(item,"solar_cost_eur",day->solar_cost_eur);
    cJSON_AddBoolToObject(item,"active",day->active);cJSON_AddNumberToObject(item,"flags",day->flags);
    cJSON_AddItemToArray(rows,item);(*shown)++;
}
#include "session_snapshot.inc"

static esp_err_t sessions_handler(httpd_req_t *req){
    session_filter_t filter;unsigned offset;char label[32];
    if(!session_query(req,&filter,&offset,label))return httpd_resp_send_err(req,HTTPD_400_BAD_REQUEST,"Zeitraum pruefen.");
    session_snapshot_t *copy=session_snapshot_create(&filter,0);if(!copy)return send_json(req,NULL);
    cJSON *o=cJSON_CreateObject();if(!o){free(copy);return send_json(req,NULL);}
    cJSON *rows=cJSON_AddArrayToObject(o,"days");unsigned count=0,shown=0;double grid=0,solar=0,cost=0,grid_cost=0,solar_cost=0;
    session_day_t day={0};
    for(unsigned i=0;i<copy->count;i++){
        const charge_session_t *r=session_snapshot_recent(copy,i);if(!session_filter_matches(r,&filter))continue;
        grid+=r->grid_wh+r->unknown_wh;solar+=r->solar_wh;cost+=r->cost_eur;
        grid_cost+=r->grid_cost_eur;solar_cost+=r->solar_cost_eur;
        if(day.sessions&&day.date!=r->start_date){session_day_emit(rows,&day,offset,&count,&shown);day=(session_day_t){0};}
        session_day_add(&day,r,copy->active_id&&r->id==copy->active_id);
    }
    if(day.sessions)session_day_emit(rows,&day,offset,&count,&shown);
    cJSON_AddStringToObject(o,"period",label);cJSON_AddNumberToObject(o,"count",count);cJSON_AddNumberToObject(o,"offset",offset);cJSON_AddNumberToObject(o,"retained",copy->retained);
    cJSON_AddNumberToObject(o,"grid_kwh",grid/1000);cJSON_AddNumberToObject(o,"solar_kwh",solar/1000);cJSON_AddNumberToObject(o,"unknown_kwh",0);cJSON_AddNumberToObject(o,"cost_eur",cost);
    cJSON_AddNumberToObject(o,"grid_cost_eur",grid_cost);cJSON_AddNumberToObject(o,"solar_cost_eur",solar_cost);
    cJSON_AddNumberToObject(o,"oldest_date",copy->oldest_date);
    free(copy);return send_json(req,o);
}
static esp_err_t session_day_handler(httpd_req_t *req){
    char query[96]={0},date_text[16]={0},offset_text[16]={0};
    if(httpd_req_get_url_query_len(req)>=sizeof(query)||httpd_req_get_url_query_str(req,query,sizeof(query))!=ESP_OK||
       httpd_query_key_value(query,"date",date_text,sizeof(date_text))!=ESP_OK||strlen(date_text)!=8)
        return httpd_resp_send_err(req,HTTPD_400_BAD_REQUEST,"Datum als YYYYMMDD angeben.");
    uint32_t date=0;
    for(unsigned i=0;i<8;i++){if(date_text[i]<'0'||date_text[i]>'9')return httpd_resp_send_err(req,HTTPD_400_BAD_REQUEST,"Ungueltiges Datum.");date=date*10+(uint32_t)(date_text[i]-'0');}
    if(date/10000<2024||(date/100)%100<1||(date/100)%100>12||date%100<1||date%100>31)
        return httpd_resp_send_err(req,HTTPD_400_BAD_REQUEST,"Ungueltiges Datum.");
    unsigned offset=0;
    if(httpd_query_key_value(query,"offset",offset_text,sizeof(offset_text))==ESP_OK){
        double value;if(!parse_number(offset_text,0,CHARGE_SESSION_CAPACITY,&value)||value!=floor(value))
            return httpd_resp_send_err(req,HTTPD_400_BAD_REQUEST,"Ungueltiger Offset.");
        offset=(unsigned)value;
    }
    session_snapshot_t *copy=session_snapshot_create(NULL,date);if(!copy)return send_json(req,NULL);
    cJSON *o=cJSON_CreateObject();if(!o){free(copy);return send_json(req,NULL);}
    cJSON *rows=cJSON_AddArrayToObject(o,"sessions");unsigned count=0,shown=0;
    for(unsigned i=0;i<copy->count;i++){
        const charge_session_t *r=session_snapshot_recent(copy,i);if(r->start_date!=date)continue;
        if(count++<offset||shown>=20)continue;
        cJSON *item=cJSON_CreateObject();if(!item)continue;
        cJSON_AddNumberToObject(item,"id",r->id);
        cJSON_AddNumberToObject(item,"start_epoch",r->start_epoch);cJSON_AddNumberToObject(item,"end_epoch",r->end_epoch);
        cJSON_AddNumberToObject(item,"charging_s",r->charging_ms/1000);
        cJSON_AddNumberToObject(item,"energy_kwh",(r->grid_wh+r->solar_wh+r->unknown_wh)/1000);
        cJSON_AddNumberToObject(item,"grid_kwh",(r->grid_wh+r->unknown_wh)/1000);cJSON_AddNumberToObject(item,"solar_kwh",r->solar_wh/1000);
        cJSON_AddNumberToObject(item,"unknown_kwh",0);cJSON_AddNumberToObject(item,"cost_eur",r->cost_eur);
        cJSON_AddNumberToObject(item,"grid_cost_eur",r->grid_cost_eur);cJSON_AddNumberToObject(item,"solar_cost_eur",r->solar_cost_eur);
        cJSON_AddNumberToObject(item,"flags",r->flags);
        cJSON_AddBoolToObject(item,"active",copy->active_id&&r->id==copy->active_id);
        cJSON_AddItemToArray(rows,item);shown++;
    }
    cJSON_AddNumberToObject(o,"date",date);cJSON_AddNumberToObject(o,"count",count);cJSON_AddNumberToObject(o,"offset",offset);
    free(copy);return send_json(req,o);
}
static void session_time(uint32_t stamp,char out[24]){
    out[0]=0;if(!stamp)return;time_t t=stamp;struct tm tm;localtime_r(&t,&tm);strftime(out,24,"%Y-%m-%d %H:%M:%S",&tm);
}
static esp_err_t csv_handler(httpd_req_t *req) {
    session_filter_t filter;unsigned offset;char label[32];
    if(!session_query(req,&filter,&offset,label))return httpd_resp_send_err(req,HTTPD_400_BAD_REQUEST,"Zeitraum pruefen.");
    session_snapshot_t *copy=session_snapshot_create(&filter,0);if(!copy)return send_json(req,NULL);
    char attachment[96];snprintf(attachment,sizeof(attachment),"attachment; filename=TeeNet-Ladesitzungen-%s.csv",label);
    httpd_resp_set_type(req,"text/csv; charset=utf-8");httpd_resp_set_hdr(req,"Content-Disposition",attachment);
    esp_err_t err=httpd_resp_sendstr_chunk(req,"\xEF\xBB\xBF" "Sitzung;Beginn;Letzte_Ladung;Ladezeit_Minuten;Energie_kWh;Netz_kWh;Netz_Kosten_EUR;Netz_Prozent;Solar_Akku_kWh;Solar_Akku_Kosten_EUR;Solar_Akku_Prozent;Kosten_EUR;Status\r\n");
    char line[384],start[24],end[24];double grid=0,solar=0,cost=0,grid_cost=0,solar_cost=0;
    for(unsigned i=copy->count;i>0&&err==ESP_OK;i--){
        const charge_session_t *r=session_snapshot_recent(copy,i-1);if(!session_filter_matches(r,&filter))continue;
        double total=(double)r->grid_wh+r->solar_wh+r->unknown_wh;
        double session_grid=r->grid_wh+r->unknown_wh,session_cost=r->cost_eur;
        grid+=session_grid;solar+=r->solar_wh;cost+=session_cost;
        grid_cost+=r->grid_cost_eur;solar_cost+=r->solar_cost_eur;
        session_time(r->start_epoch,start);session_time(r->end_epoch,end);
        const char *status=copy->active_id&&r->id==copy->active_id?"Aktiv":"Abgeschlossen";
        snprintf(line,sizeof(line),"%lu;%s;%s;%.1f;%.6f;%.6f;%.4f;%.2f;%.6f;%.4f;%.2f;%.4f;%s\r\n",(unsigned long)r->id,start,end,r->charging_ms/60000.0,total/1000,session_grid/1000,r->grid_cost_eur,total?session_grid*100/total:0,r->solar_wh/1000,r->solar_cost_eur,total?r->solar_wh*100/total:0,session_cost,status);
        for(char *p=line;*p;p++)if(*p=='.')*p=',';
        err=httpd_resp_sendstr_chunk(req,line);
    }
    if(err==ESP_OK){double total=grid+solar;snprintf(line,sizeof(line),"SUMME;;;;%.6f;%.6f;%.4f;%.2f;%.6f;%.4f;%.2f;%.4f;\r\n",total/1000,grid/1000,grid_cost,total?grid*100/total:0,solar/1000,solar_cost,total?solar*100/total:0,cost);for(char *p=line;*p;p++)if(*p=='.')*p=',';err=httpd_resp_sendstr_chunk(req,line);}
    free(copy);if(err==ESP_OK)err=httpd_resp_send_chunk(req,NULL,0);return err;
}




extern const uint8_t logo_start[] asm("_binary_teennet_logo_png_start"),logo_end[] asm("_binary_teennet_logo_png_end");

extern const uint8_t hardware_start[] asm("_binary_hardware_plan_svg_start"),hardware_end[] asm("_binary_hardware_plan_svg_end");
extern const uint8_t wiring_start[] asm("_binary_wiring_plan_jpg_start"),wiring_end[] asm("_binary_wiring_plan_jpg_end");

extern const uint8_t html_gz_start[] asm("_binary_dashboard_html_gz_start"),html_gz_end[] asm("_binary_dashboard_html_gz_end");
extern const uint8_t css_gz_start[] asm("_binary_dashboard_css_gz_start"),css_gz_end[] asm("_binary_dashboard_css_gz_end");
extern const uint8_t js_gz_start[] asm("_binary_dashboard_js_gz_start"),js_gz_end[] asm("_binary_dashboard_js_gz_end");
extern const uint8_t qr_gz_start[] asm("_binary_qrcode_js_gz_start"),qr_gz_end[] asm("_binary_qrcode_js_gz_end");
extern const uint8_t guide_gz_start[] asm("_binary_guide_html_gz_start"),guide_gz_end[] asm("_binary_guide_html_gz_end");
extern const uint8_t guide_js_gz_start[] asm("_binary_guide_js_gz_start"),guide_js_gz_end[] asm("_binary_guide_js_gz_end");


extern const uint8_t features_gz_start[] asm("_binary_features_js_gz_start"),features_gz_end[] asm("_binary_features_js_gz_end");

extern const uint8_t health_gz_start[] asm("_binary_health_js_gz_start"),health_gz_end[] asm("_binary_health_js_gz_end");

extern const uint8_t terms_js_gz_start[] asm("_binary_terms_js_gz_start"),terms_js_gz_end[] asm("_binary_terms_js_gz_end");
extern const uint8_t evcc_js_gz_start[] asm("_binary_evcc_test_js_gz_start"),evcc_js_gz_end[] asm("_binary_evcc_test_js_gz_end");
extern const uint8_t firmware_gz_start[] asm("_binary_firmware_js_gz_start"),firmware_gz_end[] asm("_binary_firmware_js_gz_end");
extern const uint8_t expert_schema_gz_start[] asm("_binary_expert_schema_json_gz_start"),expert_schema_gz_end[] asm("_binary_expert_schema_json_gz_end");

static esp_err_t asset_send(httpd_req_t *req) {

    const uint8_t *start=html_gz_start,*end=html_gz_end; const char *type="text/html; charset=utf-8";

    if(!strcmp(req->uri,"/dashboard.css")) start=css_gz_start,end=css_gz_end,type="text/css; charset=utf-8";

    if(!strcmp(req->uri,"/dashboard.js")) start=js_gz_start,end=js_gz_end,type="application/javascript; charset=utf-8";
    if(!strcmp(req->uri,"/health.js")) start=health_gz_start,end=health_gz_end,type="application/javascript; charset=utf-8";
    if(!strcmp(req->uri,"/firmware.js")) start=firmware_gz_start,end=firmware_gz_end,type="application/javascript; charset=utf-8";
    if(!strcmp(req->uri,"/terms.js")) start=terms_js_gz_start,end=terms_js_gz_end,type="application/javascript; charset=utf-8";
    else if(!strcmp(req->uri,"/evcc-test.js")) start=evcc_js_gz_start,end=evcc_js_gz_end,type="application/javascript; charset=utf-8";
    if(!strcmp(req->uri,"/features.js")) start=features_gz_start,end=features_gz_end,type="application/javascript; charset=utf-8";
    if(!strcmp(req->uri,"/qrcode.js")) start=qr_gz_start,end=qr_gz_end,type="application/javascript; charset=utf-8";
    if(!strcmp(req->uri,"/guide.js")) start=guide_js_gz_start,end=guide_js_gz_end,type="application/javascript; charset=utf-8";
    if(!strcmp(req->uri,"/expert-schema.json")) start=expert_schema_gz_start,end=expert_schema_gz_end,type="application/json; charset=utf-8";

    if(!strcmp(req->uri,"/teennet-logo.png")) start=logo_start,end=logo_end,type="image/png";

    if(!strcmp(req->uri,"/hardware-plan.svg")) start=hardware_start,end=hardware_end,type="image/svg+xml";
    if(!strcmp(req->uri,"/wiring-plan.jpg")) start=wiring_start,end=wiring_end,type="image/jpeg";


    if(!strcmp(req->uri,"/guide")) start=guide_gz_start,end=guide_gz_end,type="text/html; charset=utf-8";

    bool gzip=start!=logo_start&&start!=hardware_start&&start!=wiring_start;
    size_t length=(size_t)(end-start)-(gzip?0:1);
    if(gzip){
        char encoding[256];esp_err_t header=httpd_req_get_hdr_value_str(req,"Accept-Encoding",encoding,sizeof(encoding));
        if((header!=ESP_OK&&header!=ESP_ERR_NOT_FOUND)||!http_accepts_gzip(header==ESP_OK?encoding:NULL)){
            httpd_resp_set_hdr(req,"Vary","Accept-Encoding");httpd_resp_set_status(req,"406 Not Acceptable");
            return httpd_resp_sendstr(req,"Diese Oberflaeche benoetigt einen Browser mit GZIP-Unterstuetzung. Die JSON-API bleibt verfuegbar.");
        }
        httpd_resp_set_hdr(req,"Content-Encoding","gzip");
        httpd_resp_set_hdr(req,"Vary","Accept-Encoding");
    }

    char etag[128],previous_etag[128];
    snprintf(etag,sizeof(etag),"\"%s:%.80s:%s\"",EMS_BUILD_ID,req->uri,gzip?"gzip":"plain");
    httpd_resp_set_hdr(req,"Cache-Control","no-cache");
    httpd_resp_set_hdr(req,"ETag",etag);
    if(httpd_req_get_hdr_value_str(req,"If-None-Match",previous_etag,sizeof(previous_etag))==ESP_OK && !strcmp(previous_etag,etag)) {
        httpd_resp_set_status(req,"304 Not Modified");
        return httpd_resp_send(req,NULL,0);
    }

    httpd_resp_set_type(req,type); httpd_resp_set_hdr(req,"Cache-Control","no-cache"); httpd_resp_set_hdr(req,"X-Content-Type-Options","nosniff");

    httpd_resp_set_hdr(req,"Content-Security-Policy","default-src 'self'; script-src 'self'; style-src 'self' 'unsafe-inline'; connect-src 'self'; img-src 'self' data:; frame-ancestors 'none'; base-uri 'none'");

    /* Content-Length avoids three small TCP writes for every 2 KB chunk.
       Large pictures run on a separate worker, so slow image downloads cannot
       block status, control, or the scripts needed to start the dashboard. */
    return httpd_resp_send(req,(const char *)start,length);

}

static SemaphoreHandle_t image_slots;

static void image_worker(void *arg) {
    httpd_req_t *req=arg;
    httpd_handle_t server=req->handle;
    int sock=httpd_req_to_sockfd(req);
    esp_err_t sent=asset_send(req);
    httpd_req_async_handler_complete(req);
    if(sent!=ESP_OK) httpd_sess_trigger_close(server,sock);
    xSemaphoreGive(image_slots);
    vTaskDelete(NULL);
}

static esp_err_t asset_handler(httpd_req_t *req) {
    bool picture=!strcmp(req->uri,"/teennet-logo.png") || !strcmp(req->uri,"/wiring-plan.jpg");
    if(!picture) return asset_send(req);
    if(xSemaphoreTake(image_slots,0)!=pdTRUE) {
        httpd_resp_set_status(req,"503 Service Unavailable");
        httpd_resp_set_hdr(req,"Retry-After","2");
        return httpd_resp_sendstr(req,"Bilduebertragung ausgelastet. Bitte erneut laden.");
    }
    httpd_req_t *copy=NULL;
    esp_err_t err=httpd_req_async_handler_begin(req,&copy);
    if(err!=ESP_OK) { xSemaphoreGive(image_slots); return err; }
    if(xTaskCreate(image_worker,"web_image",4096,copy,3,NULL)!=pdPASS) {
        httpd_req_async_handler_complete(copy);
        xSemaphoreGive(image_slots);
        return httpd_resp_send_err(req,HTTPD_500_INTERNAL_SERVER_ERROR,"Bildspeicher nicht verfuegbar.");
    }
    return ESP_OK;
}

static esp_err_t web_socket_open(httpd_handle_t server,int sock) {
    (void)server;
    int enabled=1;
    return setsockopt(sock,IPPROTO_TCP,TCP_NODELAY,&enabled,sizeof(enabled))==0?ESP_OK:ESP_FAIL;
}

/* On-demand discovery uses one extra socket at a time. It never occupies
   the HTTP server task or changes the selected meter before user selection. */
static bool meter_port_open_wait(uint32_t ip,uint16_t port,unsigned timeout_ms) {
    int sock=socket(AF_INET,SOCK_STREAM,IPPROTO_IP);if(sock<0)return false;
    int flags=fcntl(sock,F_GETFL,0);bool ok=false;
    if(flags>=0 && fcntl(sock,F_SETFL,flags|O_NONBLOCK)==0){
        struct sockaddr_in addr={.sin_family=AF_INET,.sin_port=htons(port)};addr.sin_addr.s_addr=htonl(ip);
        int connected=connect(sock,(struct sockaddr *)&addr,sizeof(addr));
        if(connected==0)ok=true;
        else if(errno==EINPROGRESS || errno==EAGAIN){
            /* Weak but usable WLAN links can need more than 140 ms for the
               initial TCP handshake. Keep the scan bounded, but do not miss
               a real meter merely because one packet was retried. */
            fd_set writes;FD_ZERO(&writes);FD_SET(sock,&writes);struct timeval timeout={.tv_sec=timeout_ms/1000,.tv_usec=(timeout_ms%1000)*1000};
            if(select(sock+1,NULL,&writes,NULL,&timeout)>0){int error=0;socklen_t size=sizeof(error);
                ok=getsockopt(sock,SOL_SOCKET,SO_ERROR,&error,&size)==0 && error==0;}
        }
    }
    close(sock);return ok;
}
static bool meter_port_open(uint32_t ip,uint16_t port){return meter_port_open_wait(ip,port,300);}
#include "phase_shelly_network.inc"
#include "phase_output_network.inc"
static int shelly_discovery_get(const char *host,const char *path,char *body,size_t capacity) {
    char url[100];snprintf(url,sizeof(url),"http://%s%s",host,path);
    return http_get_body(url,body,capacity,1500);
}
static void shelly_found_add(const char *host,const char *type,float watts,bool locked) {
    LOCK();
    for(unsigned i=0;i<shelly_found_count;i++)if(!strcmp(shelly_found[i].host,host)){UNLOCK();return;}
    if(shelly_found_count<12){
        shelly_found_t *item=&shelly_found[shelly_found_count++];
        strcpy(item->host,host);strcpy(item->type,type);item->watts=watts;item->at=now_ms();item->locked=locked;
    }
    if(locked)shelly_locked_count++;
    UNLOCK();
}
static void shelly_scan_task(void *arg) {
    (void)arg;esp_netif_ip_info_t info;esp_netif_t *station=esp_netif_get_handle_from_ifkey("WIFI_STA_DEF");
    char *body=(scan_tasmota||scan_phase_shelly)?malloc(8192):NULL;
    bool complete=false;
    if(((!scan_tasmota&&!scan_phase_shelly) || body) && station && esp_netif_get_ip_info(station,&info)==ESP_OK && info.ip.addr){
        uint32_t local=ntohl(info.ip.addr),mask=ntohl(info.netmask.addr);
        /* Bound larger home networks to the ESP's local /24; honor smaller subnets. */
        if(mask<0xffffff00U)mask=0xffffff00U;
        uint32_t first=(local&mask)+1,last=(local&mask)|~mask;
        int64_t deadline=now_ms()+90000;
        for(uint32_t candidate=first;candidate<last;candidate++){
            LOCK();bool stop=!wifi_online || restarting || ota_in_progress || now_ms()>deadline;UNLOCK();
            if(stop)break;
            if(candidate!=local && meter_port_open(candidate,(scan_tasmota||scan_phase_shelly)?80:502)){
                char host[16],type[20];snprintf(host,sizeof(host),"%u.%u.%u.%u",(unsigned)(candidate>>24),
                    (unsigned)((candidate>>16)&255),(unsigned)((candidate>>8)&255),(unsigned)(candidate&255));
                if(scan_phase_shelly){
                    shelly_phase_identity_t identity;
                    int code=shelly_discovery_get(host,"/rpc/Shelly.GetDeviceInfo",body,8192);
                    if(code==200&&shelly_phase_identity(body,&identity)){
                        bool checked=false;shelly_phase_feedback_t feedback;
                        if(identity.profile!=255&&!identity.protected_device)
                            checked=shelly_discovery_get(host,"/rpc/Shelly.GetStatus",body,8192)==200&&
                                shelly_phase_feedback_for(body,&feedback,identity.inputs,false);
                        LOCK();
                        if(phase_shelly_found_count<12){
                            unsigned index=phase_shelly_found_count++;
                            strcpy(phase_shelly_found[index].host,host);
                            phase_shelly_found[index].identity=identity;
                            phase_shelly_found[index].contacts_checked=checked;
                        }
                        UNLOCK();
                    }
                }else if(scan_tasmota){
                    int code=shelly_discovery_get(host,"/cm?cmnd=Status%2010",body,8192);float watts;
                    if(code==401 || code==403){LOCK();shelly_locked_count++;UNLOCK();}
                    else if(code==200 && house_power_parse(body,"tasmota","",&watts))shelly_found_add(host,"tasmota",watts,false);
                }else{
                    shelly_modbus_reading_t reading;
                    strcpy(type,"shelly_gen2");
                    if(read_shelly_modbus(host,type,&reading,1200))shelly_found_add(host,type,reading.active_power_w,false);
                    else {strcpy(type,"shelly_em1");if(read_shelly_modbus(host,type,&reading,1200))shelly_found_add(host,type,reading.active_power_w,false);}
                }
            }
            LOCK();shelly_scan_progress=(candidate-first+1)*100/(last-first);UNLOCK();
            if(candidate+1==last)complete=true;
            vTaskDelay(ticks(25));
        }
    }
    free(body);LOCK();shelly_scan_partial=!complete;shelly_scan_running=false;UNLOCK();vTaskDelete(NULL);
}
#include "huawei_discovery.inc"

static esp_err_t shelly_scan_get(httpd_req_t *req) {
    cJSON *o=cJSON_CreateObject();if(!o)return send_json(req,NULL);
    cJSON *items=cJSON_AddArrayToObject(o,"meters");LOCK();
    cJSON_AddBoolToObject(o,"running",shelly_scan_running);cJSON_AddBoolToObject(o,"partial",shelly_scan_partial);
    cJSON_AddStringToObject(o,"family",scan_phase_shelly?"shelly_switch":scan_tasmota?"tasmota":"shelly");
    cJSON *devices=cJSON_AddArrayToObject(o,"devices");
    for(unsigned i=0;scan_phase_shelly&&i<phase_shelly_found_count;i++){
        cJSON *device=cJSON_CreateObject();if(!device)break;
        const shelly_phase_identity_t *identity=&phase_shelly_found[i].identity;
        cJSON_AddStringToObject(device,"host",phase_shelly_found[i].host);
        cJSON_AddStringToObject(device,"name",identity->name);cJSON_AddStringToObject(device,"id",identity->id);
        cJSON_AddStringToObject(device,"model",identity->model);
        cJSON_AddBoolToObject(device,"protected",identity->protected_device);
        cJSON_AddBoolToObject(device,"compatible",identity->profile!=255);
        cJSON_AddNumberToObject(device,"profile",identity->profile);
        cJSON_AddNumberToObject(device,"inputs",identity->inputs);
        cJSON_AddBoolToObject(device,"modbus",identity->profile==4);
        cJSON_AddBoolToObject(device,"contacts_checked",phase_shelly_found[i].contacts_checked);
        cJSON_AddItemToArray(devices,device);
    }
    cJSON_AddNumberToObject(o,"progress",shelly_scan_progress);cJSON_AddNumberToObject(o,"locked",shelly_locked_count);
    for(unsigned i=0;i<shelly_found_count;i++){
        cJSON *item=cJSON_CreateObject();if(!item)break;
        cJSON_AddStringToObject(item,"host",shelly_found[i].host);cJSON_AddStringToObject(item,"type",shelly_found[i].type);
        if(shelly_found[i].locked)cJSON_AddNullToObject(item,"watts");else cJSON_AddNumberToObject(item,"watts",shelly_found[i].watts);
        cJSON_AddBoolToObject(item,"locked",shelly_found[i].locked);cJSON_AddNumberToObject(item,"age_ms",now_ms()-shelly_found[i].at);cJSON_AddItemToArray(items,item);
    }
    UNLOCK();return send_json(req,o);
}
static esp_err_t shelly_scan_post(httpd_req_t *req) {
    if(!authorized(req))return ESP_OK;
    cJSON *o=read_json(req);if(!o)return ESP_OK;
    cJSON *family=cJSON_GetObjectItemCaseSensitive(o,"family");
    bool valid=cJSON_IsString(family) && (!strcmp(family->valuestring,"tasmota") || !strcmp(family->valuestring,"shelly") || !strcmp(family->valuestring,"shelly_switch"));
    bool tasmota=valid && !strcmp(family->valuestring,"tasmota"),actuator=valid&&!strcmp(family->valuestring,"shelly_switch");cJSON_Delete(o);
    if(!valid)return httpd_resp_send_err(req,HTTPD_400_BAD_REQUEST,"Zaehlerfamilie auswaehlen.");
    LOCK();bool available=wifi_online && !wifi_ap_forced && !restarting && !ota_in_progress;
    bool running=shell_scan_running || shelly_scan_running || meter_preview.running || huawei_scan_running || huawei_preview_running;
    bool recent=shelly_scan_started && now_ms()-shelly_scan_started<60000;
    if(available && !running && !recent){scan_tasmota=tasmota;scan_phase_shelly=actuator;phase_shelly_found_count=0;shelly_scan_running=true;shelly_scan_started=now_ms();shelly_scan_progress=shelly_found_count=shelly_locked_count=0;shelly_scan_partial=false;}
    UNLOCK();
    if(!available)return httpd_resp_send_err(req,HTTPD_400_BAD_REQUEST,"Zuerst mit dem Heimnetz verbinden.");
    if(running || recent)return httpd_resp_send_err(req,HTTPD_400_BAD_REQUEST,"Suche laeuft oder wurde gerade beendet. Bitte kurz warten.");
    if(xTaskCreate(shelly_scan_task,"shelly_scan",6144,NULL,3,NULL)!=pdPASS){
        LOCK();shelly_scan_running=false;UNLOCK();return httpd_resp_send_err(req,HTTPD_500_INTERNAL_SERVER_ERROR,"Suche konnte nicht gestartet werden.");
    }
    return shelly_scan_get(req);
}

static void meter_preview_task(void *arg) {
    (void)arg;
    char host[64],type[20],path[96];
    bool wallbox;uint8_t unit;LOCK();strcpy(host,meter_preview.host);strcpy(type,meter_preview.type);strcpy(path,meter_preview.path);wallbox=meter_preview.wallbox;unit=meter_preview.unit_id;
    bool available=wifi_online && !restarting && !ota_in_progress;UNLOCK();
    float watts=0;int code=0;bool ok=false;
    if(available && shelly_meter_type(type)){
        shelly_modbus_reading_t reading;
        ok=read_shelly_modbus(host,type,&reading,2500);if(ok){watts=reading.active_power_w;code=200;}
    }else if(available && !strcmp(type,"em24_tcp")){
        em24_reading_t reading;ok=read_em24_modbus(host,unit,&reading,2500);
        if(ok){watts=reading.active_power_w;code=200;}
    }else if(available){
        char url[256];char *body=malloc(8192);
        if(body && house_query_url(type,host,path,url,sizeof(url))){code=http_get_body(url,body,8192,3500);if(code==200)ok=house_power_parse(body,type,path,&watts);}
        free(body);
    }
    if(wallbox && ok && !strcmp(type,"shelly_em1")){unsigned phases;LOCK();phases=settings.charge_phases;UNLOCK();watts*=phases;}
    LOCK();meter_preview.watts=watts;meter_preview.code=code;meter_preview.ok=ok;
    meter_preview.at=now_ms();meter_preview.running=false;UNLOCK();vTaskDelete(NULL);
}
static esp_err_t meter_preview_get(httpd_req_t *req) {
    cJSON *o=cJSON_CreateObject();if(!o)return send_json(req,NULL);
    LOCK();
    cJSON_AddBoolToObject(o,"running",meter_preview.running);
    cJSON_AddBoolToObject(o,"ok",meter_preview.ok && fresh(now_ms(),meter_preview.at,15000));
    cJSON_AddStringToObject(o,"host",meter_preview.host);cJSON_AddStringToObject(o,"type",meter_preview.type);
    cJSON_AddStringToObject(o,"path",meter_preview.path);
    cJSON_AddStringToObject(o,"role",meter_preview.wallbox?"wallbox":"house");
    cJSON_AddNumberToObject(o,"unit_id",meter_preview.unit_id);
    cJSON_AddNumberToObject(o,"watts",meter_preview.watts);
    cJSON_AddNumberToObject(o,"age_ms",meter_preview.at?now_ms()-meter_preview.at:-1);
    cJSON_AddNumberToObject(o,"http",meter_preview.code);
    UNLOCK();return send_json(req,o);
}
static esp_err_t meter_preview_post(httpd_req_t *req) {
    if(!authorized(req))return ESP_OK;
    cJSON *o=read_json(req);if(!o)return ESP_OK;
    cJSON *host=cJSON_GetObjectItemCaseSensitive(o,"host"),*type=cJSON_GetObjectItemCaseSensitive(o,"type"),*path=cJSON_GetObjectItemCaseSensitive(o,"path"),*role=cJSON_GetObjectItemCaseSensitive(o,"role");
    bool valid=cJSON_IsString(host) && host->valuestring[0] && strlen(host->valuestring)<64 &&
        cJSON_IsString(type) && strlen(type->valuestring)<20 && cJSON_IsString(path) && strlen(path->valuestring)<96 &&
        cJSON_IsString(role) && (!strcmp(role->valuestring,"house") || !strcmp(role->valuestring,"wallbox"));
    bool em24=valid && !strcmp(type->valuestring,"em24_tcp");
    cJSON *unit=cJSON_GetObjectItemCaseSensitive(o,"unit_id");
    int unit_id=1;
    if(em24){valid=cJSON_IsNumber(unit) && unit->valuedouble==unit->valueint && unit->valueint>=1 && unit->valueint<=247;if(valid)unit_id=unit->valueint;}
    if(valid)for(const char *p=host->valuestring;*p;p++)if(!((*p>='a'&&*p<='z')||(*p>='A'&&*p<='Z')||(*p>='0'&&*p<='9')||*p=='.'||*p=='-'||(em24 && *p==':')))valid=false;
    char standard[256]={0};
    if(valid && !em24 && !shelly_meter_type(type->valuestring))valid=house_query_url(type->valuestring,host->valuestring,"",standard,sizeof(standard));
    /* Only known read-only status endpoints, never an arbitrary HTTP command. */
    if(valid && !em24 && !shelly_meter_type(type->valuestring) && house_power_is_url(path->valuestring))valid=standard[0] && !strcmp(standard,path->valuestring);
    if(!valid){cJSON_Delete(o);return httpd_resp_send_err(req,HTTPD_400_BAD_REQUEST,"Zaehleradresse und Abfrage pruefen.");}
    LOCK();bool available=wifi_online && !wifi_ap_forced && !ota_in_progress && !restarting;
    bool busy=shell_scan_running || meter_preview.running || shelly_scan_running || huawei_scan_running || huawei_preview_running ||
        (meter_preview.started && now_ms()-meter_preview.started<3000);
    if(available && !busy){
        strcpy(meter_preview.host,host->valuestring);strcpy(meter_preview.type,type->valuestring);strcpy(meter_preview.path,path->valuestring);meter_preview.wallbox=!strcmp(role->valuestring,"wallbox");
        meter_preview.unit_id=(uint8_t)unit_id;
        meter_preview.running=true;meter_preview.ok=false;meter_preview.at=0;meter_preview.started=now_ms();
    }
    UNLOCK();cJSON_Delete(o);
    if(!available)return httpd_resp_send_err(req,HTTPD_400_BAD_REQUEST,"Zuerst mit dem Heimnetz verbinden.");
    if(busy)return httpd_resp_send_err(req,HTTPD_400_BAD_REQUEST,"Netzwerksuche oder Messung laeuft. Bitte kurz warten.");
    if(xTaskCreate(meter_preview_task,"meter_preview",6144,NULL,3,NULL)!=pdPASS){
        LOCK();meter_preview.running=false;UNLOCK();return httpd_resp_send_err(req,HTTPD_500_INTERNAL_SERVER_ERROR,"Vorschau nicht gestartet.");
    }
    return meter_preview_get(req);
}
#include "terms_http.inc"
#include "evcc_http.inc"
#include "shell_limits.inc"

static void start_web(void) {

    /* Reserve sockets for meters, MQTT and the phase actuator (16 total). */
    httpd_config_t cfg=HTTPD_DEFAULT_CONFIG(); cfg.stack_size=8192; cfg.max_uri_handlers=72; cfg.max_open_sockets=5; cfg.lru_purge_enable=true; cfg.recv_wait_timeout=2; cfg.send_wait_timeout=4;
    cfg.open_fn=web_socket_open;
    image_slots=xSemaphoreCreateCounting(2,2);
    if(!image_slots) abort();

    httpd_handle_t server; ESP_ERROR_CHECK(httpd_start(&server,&cfg));web_started=true;

    const httpd_uri_t routes[]={
        {.uri="/api/terms",.method=HTTP_POST,.handler=terms_post},
        {.uri="/api/evcc/local_release",.method=HTTP_POST,.handler=evcc_release_post},
        {.uri="/api/evcc/config",.method=HTTP_GET,.handler=evcc_config_get},
        {.uri="/api/evcc/config",.method=HTTP_POST,.handler=evcc_config_post},
        {.uri="/api/evcc/status",.method=HTTP_GET,.handler=evcc_status_get},
        {.uri="/api/evcc/enabled",.method=HTTP_GET,.handler=evcc_enabled_get},
        {.uri="/api/evcc/current",.method=HTTP_POST,.handler=evcc_command_post},
        {.uri="/api/evcc/enable",.method=HTTP_POST,.handler=evcc_command_post},
        {.uri="/api/evcc/phases",.method=HTTP_POST,.handler=evcc_phases_post},
        {.uri="/api/evcc/phases",.method=HTTP_GET,.handler=evcc_phases_get},
        {.uri="/terms.js",.method=HTTP_GET,.handler=asset_handler},
        {.uri="/evcc-test.js",.method=HTTP_GET,.handler=asset_handler},
        {.uri="/api/shell/scan",.method=HTTP_GET,.handler=shell_scan_get},
        {.uri="/api/shell/scan",.method=HTTP_POST,.handler=shell_scan_post},

        {.uri="/",.method=HTTP_GET,.handler=asset_handler},{.uri="/dashboard.css",.method=HTTP_GET,.handler=asset_handler},

        {.uri="/dashboard.js",.method=HTTP_GET,.handler=asset_handler},{.uri="/health.js",.method=HTTP_GET,.handler=asset_handler},{.uri="/firmware.js",.method=HTTP_GET,.handler=asset_handler},{.uri="/features.js",.method=HTTP_GET,.handler=asset_handler},{.uri="/qrcode.js",.method=HTTP_GET,.handler=asset_handler},{.uri="/teennet-logo.png",.method=HTTP_GET,.handler=asset_handler},

        {.uri="/hardware-plan.svg",.method=HTTP_GET,.handler=asset_handler},
        {.uri="/wiring-plan.jpg",.method=HTTP_GET,.handler=asset_handler},{.uri="/guide",.method=HTTP_GET,.handler=asset_handler},
        {.uri="/guide.js",.method=HTTP_GET,.handler=asset_handler},
        {.uri="/expert-schema.json",.method=HTTP_GET,.handler=asset_handler},


        {.uri="/api/status",.method=HTTP_GET,.handler=status_handler},

        {.uri="/api/theme",.method=HTTP_GET,.handler=theme_get},{.uri="/api/theme",.method=HTTP_POST,.handler=theme_post},

        {.uri="/api/history",.method=HTTP_GET,.handler=history_handler},{.uri="/api/sessions",.method=HTTP_GET,.handler=sessions_handler},{.uri="/api/sessions/day",.method=HTTP_GET,.handler=session_day_handler},{.uri="/api/export.csv",.method=HTTP_GET,.handler=csv_handler},

        {.uri="/api/expert",.method=HTTP_POST,.handler=expert_post},
        {.uri="/api/config",.method=HTTP_GET,.handler=config_get},{.uri="/api/config",.method=HTTP_POST,.handler=config_post},
        {.uri="/api/events",.method=HTTP_GET,.handler=events_handler},
        {.uri="/api/backup",.method=HTTP_GET,.handler=backup_handler},
        {.uri="/api/backup/system",.method=HTTP_GET,.handler=system_backup_handler},
        {.uri="/api/update/github",.method=HTTP_GET,.handler=github_get},
        {.uri="/api/update/github",.method=HTTP_POST,.handler=github_post},
        {.uri="/api/restore",.method=HTTP_POST,.handler=restore_handler},
        {.uri="/api/plan",.method=HTTP_POST,.handler=plan_handler},
        {.uri="/api/huawei/scan",.method=HTTP_GET,.handler=huawei_scan_get},
        {.uri="/api/huawei/scan",.method=HTTP_POST,.handler=huawei_scan_post},
        {.uri="/api/huawei/preview",.method=HTTP_GET,.handler=huawei_preview_get},
        {.uri="/api/huawei/preview",.method=HTTP_POST,.handler=huawei_preview_post},
        {.uri="/api/meters/scan",.method=HTTP_GET,.handler=shelly_scan_get},{.uri="/api/meters/scan",.method=HTTP_POST,.handler=shelly_scan_post},
        {.uri="/api/meters/preview",.method=HTTP_GET,.handler=meter_preview_get},{.uri="/api/meters/preview",.method=HTTP_POST,.handler=meter_preview_post},

        {.uri="/api/control",.method=HTTP_POST,.handler=control_handler},{.uri="/api/time",.method=HTTP_POST,.handler=time_handler},{.uri="/api/energy/reset",.method=HTTP_POST,.handler=energy_reset_handler},{.uri="/api/sessions/reset",.method=HTTP_POST,.handler=sessions_reset_handler},

        {.uri="/api/phase-test",.method=HTTP_POST,.handler=phase_test_handler},
        {.uri="/api/reboot",.method=HTTP_POST,.handler=reboot_handler},{.uri="/api/factory-reset",.method=HTTP_POST,.handler=factory_handler},

        {.uri="/api/wifi/ap",.method=HTTP_POST,.handler=ap_handler},{.uri="/api/wifi/scan",.method=HTTP_GET,.handler=wifi_scan_handler},{.uri="/api/ota",.method=HTTP_POST,.handler=ota_handler}};

    for(size_t i=0;i<sizeof(routes)/sizeof(routes[0]);i++) ESP_ERROR_CHECK(httpd_register_uri_handler(server,&routes[i]));

}

static void wifi_event(void *arg,esp_event_base_t base,int32_t id,void *data) {

    if(base==WIFI_EVENT && id==WIFI_EVENT_STA_START) {

        LOCK(); bool connect=settings.wifi_ssid[0] && !wifi_scanning && !wifi_ap_forced;

        if(connect && !wifi_lost_at) wifi_lost_at=now_ms();

        UNLOCK();

        if(connect) esp_wifi_connect();

    }

    if(base==WIFI_EVENT && id==WIFI_EVENT_STA_DISCONNECTED) { LOCK(); wifi_online=false; station_ip[0]=0; if(!wifi_lost_at) wifi_lost_at=now_ms(); house_power_at=0;if(network_house_meter() || settings.house_rs485_interface)house_current_at=0;if(network_wallbox_meter() || settings.meter_rs485_interface)meter_at=actual_at=power_at=0; UNLOCK(); }

    if(base==IP_EVENT && id==IP_EVENT_STA_GOT_IP) {

        ip_event_got_ip_t *event=data; LOCK(); wifi_online=true; wifi_lost_at=0; snprintf(station_ip,sizeof(station_ip),IPSTR,IP2STR(&event->ip_info.ip)); UNLOCK();

        if(sntp_ready) esp_netif_sntp_start();

    }

}

static void start_wifi(void) {

    ESP_ERROR_CHECK(esp_netif_init()); ESP_ERROR_CHECK(esp_event_loop_create_default()); esp_netif_create_default_wifi_ap();
    esp_netif_t *station=esp_netif_create_default_wifi_sta();
    ESP_ERROR_CHECK(esp_netif_set_hostname(station,DEVICE_HOSTNAME));

    esp_sntp_config_t time_cfg=ESP_NETIF_SNTP_DEFAULT_CONFIG("pool.ntp.org"); time_cfg.start=false; sntp_ready=esp_netif_sntp_init(&time_cfg)==ESP_OK;

    wifi_init_config_t cfg=WIFI_INIT_CONFIG_DEFAULT(); ESP_ERROR_CHECK(esp_wifi_init(&cfg)); ESP_ERROR_CHECK(esp_wifi_set_storage(WIFI_STORAGE_RAM));

    ESP_ERROR_CHECK(esp_event_handler_register(WIFI_EVENT,ESP_EVENT_ANY_ID,wifi_event,NULL)); ESP_ERROR_CHECK(esp_event_handler_register(IP_EVENT,IP_EVENT_STA_GOT_IP,wifi_event,NULL));

    wifi_config_t ap={0}; strcpy((char *)ap.ap.ssid,SETUP_SSID);

    if(settings.wifi_ssid[0]) strcpy((char *)ap.ap.password,"wallboxems-setup");

    ap.ap.ssid_len=strlen(SETUP_SSID); ap.ap.channel=1; ap.ap.max_connection=4;

    ap.ap.authmode=settings.wifi_ssid[0]?WIFI_AUTH_WPA2_PSK:WIFI_AUTH_OPEN;

    ESP_ERROR_CHECK(esp_wifi_set_mode(settings.wifi_ssid[0]?WIFI_MODE_APSTA:WIFI_MODE_AP)); ESP_ERROR_CHECK(esp_wifi_set_config(WIFI_IF_AP,&ap));

    if(settings.wifi_ssid[0]) {

        wifi_config_t sta={0}; strcpy((char *)sta.sta.ssid,settings.wifi_ssid); strcpy((char *)sta.sta.password,settings.wifi_password);
        /* Fritz mesh nodes share one SSID. Scan every channel and choose the
           strongest matching AP instead of reusing a weak cached channel. */
        sta.sta.scan_method=WIFI_ALL_CHANNEL_SCAN;
        sta.sta.sort_method=WIFI_CONNECT_AP_BY_SIGNAL;

        ESP_ERROR_CHECK(esp_wifi_set_config(WIFI_IF_STA,&sta));

        /* Configure AP credentials before switching to STA-only. No setup AP

           is broadcast on a normal boot with saved home-network credentials. */

        ESP_ERROR_CHECK(esp_wifi_set_mode(WIFI_MODE_STA));

        wifi_lost_at=now_ms();

    }

    ESP_ERROR_CHECK(esp_wifi_start());
    /* Mains-powered controller: avoid modem-sleep latency during DLB/MQTT/HTTP. */
    ESP_ERROR_CHECK(esp_wifi_set_ps(WIFI_PS_NONE));

}

static void maintain_wifi(int64_t now) {

    static int64_t retry_at,signal_at,weak_since,last_roam;

    /* Scans and changes of radio mode must never overlap. */

    if(xSemaphoreTake(wifi_action_lock,0)!=pdTRUE) return;

    LOCK();

    if(ap_requested) { wifi_ap_forced=true; ap_requested=false; }

    bool online=wifi_online,configured=settings.wifi_ssid[0]!=0,forced=wifi_ap_forced;
    bool charging=settings.enabled;

    ems_wifi_mode_t wanted=wifi_recovery_mode(configured,online,forced,now,wifi_lost_at);

    UNLOCK();

    wifi_mode_t desired=wanted==EMS_WIFI_AP?WIFI_MODE_AP:wanted==EMS_WIFI_STA?WIFI_MODE_STA:WIFI_MODE_APSTA;

    wifi_mode_t current=WIFI_MODE_NULL; esp_err_t err=esp_wifi_get_mode(&current);

    if(err==ESP_OK && current!=desired) {

        err=esp_wifi_set_mode(desired);

        if(err==ESP_OK) ESP_LOGI(TAG,"WLAN mode: %s",desired==WIFI_MODE_STA?"home network; AP disabled":desired==WIFI_MODE_AP?"manual/setup AP":"recovery AP and reconnect");

        else ESP_LOGE(TAG,"WLAN mode change failed: %s",esp_err_to_name(err));

    }

    LOCK(); wifi_fallback=err==ESP_OK && wanted==EMS_WIFI_AP_STA; UNLOCK();

    if(err==ESP_OK && configured && !online && !forced && now-retry_at>=ems_param_ms(EP_WIFI_RETRY)) { esp_wifi_connect(); retry_at=now; }

    /* A mains-powered idle controller may roam away from a persistently weak
       mesh node. Never interrupt an active charging session for this. */
    if(err==ESP_OK && online && !forced && !charging && now-signal_at>=ems_param_ms(EP_WIFI_CHECK)) {
        wifi_ap_record_t ap_info={0};signal_at=now;
        if(esp_wifi_sta_get_ap_info(&ap_info)==ESP_OK && ap_info.rssi<=ems_param(EP_WIFI_RSSI)) {
            if(!weak_since)weak_since=now;
            if(now-weak_since>=ems_param_ms(EP_WIFI_WEAK) && now-last_roam>=ems_param_ms(EP_WIFI_ROAM)) {
                ESP_LOGW(TAG,"Weak WLAN (%d dBm); selecting a stronger mesh AP",ap_info.rssi);
                weak_since=0;last_roam=now;esp_wifi_disconnect();retry_at=now-ems_param_ms(EP_WIFI_RETRY);
            }
        } else weak_since=0;
    } else if(!online || charging) weak_since=0;

    xSemaphoreGive(wifi_action_lock);

}

static void relay_signals_locked(int64_t now) {
    if(!settings.relay_board_enabled || !relay_ready) return;
    (void)now;
    if(!settings.phase_switch_enabled)relay_set(0,false);
}

static void status_task(void *arg) {

    int64_t checkpoint=now_ms(),publish=0,reconnect=0,console=0; double saved_total[2]={energy.store.total_wh[0],energy.store.total_wh[1]};

    while(true) {
        boot_task_seen[0]=(uint32_t)now_ms();

        int64_t now=now_ms(),epoch,midnight; uint32_t date=calendar(&epoch,&midnight); LOCK();
        restore_control_memory_locked(now,date?epoch:0,false);
        if(reset_count_pending && now>=reset_clear_at && !backup_in_progress) { UNLOCK(); reset_sequence_clear(); LOCK(); }

        opendtu_apply_locked(now);
        if(evcc_tick(&evcc_control,now)) {
            settings.enabled=false;settings.mode=MODE_OFF;event_locked("evcc_timeout");
        }
        if(evcc_control.configured && !evcc_control.enabled && settings.enabled) {
            settings.enabled=false;settings.mode=MODE_OFF;
        }
        contacts_step_locked(now);
        phase_test_tick_locked(now);
        if(!boot_validation_pending&&!boot_validation_failed)phase_step_locked(now);
        relay_signals_locked(now);
        if(settings.mode==MODE_PV && settings.zero_feed_enabled && fresh(now,house_power_at,house_meter_ttl()) && fresh(now,actual_at,wallbox_meter_ttl())) {
            settings.pv_surplus_a=pv_available_current_locked(now,house_power_w);pv_at=now;
        }
        bool assisted_start=battery_start_use && settings.battery_protect && battery_ready_locked(now) && battery_soc>settings.battery_reserve_soc;
        pv_control_step_timed(&pv_control,pv_permitted_locked(now),pv_ramp_available_phases(settings.pv_surplus_a,pv_control.target_a,actual_a,settings.charge_phases),active_min_a(),settings.max_charge_a,
            assisted_start?active_min_a():pv_start_threshold(&settings),assisted_start?0:EMS_PV_START_MS,now);
        /* Keep the anti-cycle detector on the deterministic one-second
           controller clock. HTTP rendering and frequent Wallbox requests
           must be read-only views of this state. */
        float unguarded_target=unlatched_target_locked(now);
        charge_guard_step(&charge_guard,unguarded_target>0,
            fresh(now,actual_at,wallbox_meter_ttl()),actual_a,active_min_a(),now);
        bool learn=feedback_ready(now) && fresh(now,wallbox_at,5000) && fresh(now,last_sent_current_at,5000) &&
            !charge_guard_blocked(&charge_guard,now) && !settings.phase_switch_enabled &&
            !settings.grid_guard_enabled && !(settings.evu_input_enabled && evu_contact.stable) &&
            !restart_pending() && !restarting && !ota_in_progress && !charge_plan.active;
        if(current_calibration_step(&current_calibration,&settings,learn,unguarded_target,actual_a,actual_at,now)){
            saved_settings.current_offset_a=settings.current_offset_a;calibration_dirty=true;
        }

        double power[2]={estimated_charge_power(&settings,meter_a),wallbox_w};

        int64_t expires[2]={meter_at?meter_at+wallbox_meter_ttl():0,power_at?power_at+((sdm_meter()||network_wallbox_meter())?wallbox_meter_ttl():EMS_INPUT_TTL):0};

        energy_step(&energy,now,epoch,date,midnight,power,expires);
        sessions_step(&sessions,now,epoch,date,power[0],expires[0],house_power_w,
            house_power_at?house_power_at+house_meter_ttl():0,settings.price_kwh,settings.solar_price_kwh);

        plan_phase_t previous_plan=plan_phase;
        float maximum_kw=charge_plan_maximum_kw(&settings,house_currents_ready_locked(now),evu_contact.stable);
        plan_phase=charge_plan_step(&charge_plan,epoch/1000,date!=0,settings.enabled,
            energy.store.total_wh[0],maximum_kw);
        if(!evcc_control.configured && charge_plan.active&&plan_phase==PLAN_GRID&&settings.mode!=MODE_MANUAL) {
            settings.mode=MODE_MANUAL;settings.manual_phases=settings.phase_switch_enabled?3:settings.fixed_charge_phases;settings.manual_current_a=settings.max_charge_a;
            pv_control_step(&pv_control,false,0,active_min_a(),settings.max_charge_a,now);
            event_locked("plan_grid");plan_dirty=true;plan_revision++;
        }
        if(previous_plan!=plan_phase&&(plan_phase==PLAN_COMPLETE||plan_phase==PLAN_EXPIRED)) {
            settings.enabled=false;settings.mode=MODE_OFF;plan_dirty=true;plan_revision++;
            event_locked(plan_phase==PLAN_COMPLETE?"plan_complete":"plan_expired");
        }
        uint32_t previous_event_sequence=event_log.sequence;
        ems_event_observe(&event_log,block_reason_locked(now),date?epoch/1000:0,now/1000,
            target_locked(now)*settings.charge_phases*settings.nominal_v/1000,
            fresh(now,meter_at,wallbox_meter_ttl())?power[0]/1000:NAN,
            fresh(now,house_power_at,house_meter_ttl())?house_power_w/1000:NAN,
            fresh(now,battery_soc_at,EMS_INPUT_TTL)?battery_soc:NAN);

        if(event_log.sequence!=previous_event_sequence&&diagnostic_store_add(&fault_log,ems_event_recent(&event_log,0))){fault_dirty=true;fault_revision++;}
        /* A reboot releases GPIO12. Let the phase controller confirm zero
           current and park the contactor first, also under evcc ownership. */
        bool remote_restart_ready=phase_shelly_remote()&&phase_state==PHASE_READY&&phase_shelly_restart_prepared&&
            phase_shelly_observed_locked(now)!=0&&fresh(now,actual_at,wallbox_meter_ttl())&&
            actual_a[0]>=0&&actual_a[0]<1&&actual_a[1]>=0&&actual_a[1]<1&&actual_a[2]>=0&&actual_a[2]<1;
        bool mqtt_restart_ready=!phase_mqtt_inflight&&fresh(now,actual_at,wallbox_meter_ttl())&&
            actual_a[0]>=0&&actual_a[0]<1&&actual_a[1]>=0&&actual_a[1]<1&&actual_a[2]>=0&&actual_a[2]<1&&
            (!phase_mqtt_allowed||(phase_state==PHASE_READY&&phase_mqtt_known&&phase_mqtt_assumed==3));
        bool reboot=restarting && (phase_fault_restart_safe_locked(now) || !settings.phase_switch_enabled || (phase_mqtt_remote()?mqtt_restart_ready:phase_shelly_remote()?remote_restart_ready:!relay_on[0])),online=wifi_online,session_checkpoint=sessions.checkpoint,changed=energy.store.total_wh[0]!=saved_total[0] || energy.store.total_wh[1]!=saved_total[1]; UNLOCK();
        bool flash_finished=false;
        if(xSemaphoreTake(maintenance_flash_lock,0)==pdTRUE){
        LOCK();bool persist_calibration=calibration_dirty && (reboot || now-calibration_saved_at>=1800000);
        float offset_to_save=settings.current_offset_a;UNLOCK();
        if(persist_calibration && config_storage_ok){
            nvs_handle_t nvs;if(nvs_open("wallbox_ems",NVS_READWRITE,&nvs)==ESP_OK){
                esp_err_t err=nvs_set_blob(nvs,"auto_offset",&offset_to_save,sizeof(offset_to_save));
                if(err==ESP_OK)err=nvs_commit(nvs);
                nvs_close(nvs);
                LOCK();calibration_saved_at=now;if(err==ESP_OK && settings.current_offset_a==offset_to_save)calibration_dirty=false;UNLOCK();
            }
        }

        if((now-checkpoint>=900000 && changed) || session_checkpoint || reboot) {

            bool saved=save_energy(); checkpoint=now;

            if(saved) { LOCK(); saved_total[0]=energy.store.total_wh[0]; saved_total[1]=energy.store.total_wh[1]; bool has_plan=charge_plan.active||plan_dirty; UNLOCK(); if(has_plan)save_plan(); }

        }
        LOCK();bool persist_plan=plan_dirty && now-plan_save_attempt>=60000;UNLOCK();
        if(persist_plan)save_plan();

        save_diagnostics(reboot);
        save_control_memory(reboot);
        flash_finished=true;
        xSemaphoreGive(maintenance_flash_lock);
        }
        if(reboot && flash_finished) {

            vTaskDelay(ticks(1000));

            if(factory_reset_requested) { nvs_flash_erase(); nvs_flash_erase_partition("stats"); factory_reset_requested=false; reset_press_count=0; }

            esp_restart();

        }

        maintain_wifi(now);

        if(now-publish>=ems_param_ms(EP_MQTT_PUBLISH)) { mqtt_state(); publish=now; }

        if(now-reconnect>=10000) {

            if(online && !mqtt_client) start_mqtt();

            reconnect=now;

        }

        if(now-console>=30000) {

            LOCK(); ESP_LOGI(TAG,"v%s uptime=%lld heap=%lu meter=%d mqtt=%d block=%s",VERSION,now/1000,(unsigned long)esp_get_free_heap_size(),fresh(now,meter_at,wallbox_meter_ttl()),mqtt_online,block_reason_locked(now)); UNLOCK(); console=now;

        }

        vTaskDelay(ticks(1000));

    }

}

#include "boot_check.inc"

void app_main(void) {
    esp_ota_img_states_t image_state=ESP_OTA_IMG_UNDEFINED;
    boot_rollback_armed=esp_ota_get_state_partition(esp_ota_get_running_partition(),&image_state)==ESP_OK&&image_state==ESP_OTA_IMG_PENDING_VERIFY;

    uint8_t mac[6];
    if(esp_read_mac(mac,ESP_MAC_WIFI_STA)==ESP_OK)
        snprintf(mqtt_device_id,sizeof(mqtt_device_id),"teenet_%02x%02x%02x%02x%02x%02x",
            mac[0],mac[1],mac[2],mac[3],mac[4],mac[5]);

    state_lock=xSemaphoreCreateMutex(); wifi_action_lock=xSemaphoreCreateMutex(); plan_save_lock=xSemaphoreCreateMutex();maintenance_flash_lock=xSemaphoreCreateMutex();phase_mqtt_send_lock=xSemaphoreCreateMutex(); if(!phase_mqtt_send_lock||!state_lock || !wifi_action_lock || !plan_save_lock||!maintenance_flash_lock) abort();

    /* Never erase settings or accumulated energy automatically on storage errors. */

    config_storage_ok=nvs_flash_init()==ESP_OK; stats_storage_ok=nvs_flash_init_partition("stats")==ESP_OK;

    reset_sequence_start();

    if(factory_reset_requested) {

        ESP_ERROR_CHECK(nvs_flash_erase()); ESP_ERROR_CHECK(nvs_flash_erase_partition("stats"));

        factory_reset_requested=false; reset_count_pending=false; reset_press_count=0;

        config_storage_ok=nvs_flash_init()==ESP_OK; stats_storage_ok=nvs_flash_init_partition("stats")==ESP_OK;

    }

    load_settings(); expert_load(); load_control_memory(); terms_load(); evcc_load(); load_energy(); load_plan(); load_diagnostics();
    if(evcc_control.configured&&charge_plan.active){charge_plan.active=false;plan_phase=PLAN_OFF;plan_dirty=true;plan_revision++;} phase_setup(); contact_setup(); setenv("TZ","CET-1CEST,M3.5.0,M10.5.0/3",1); tzset();
    LOCK();esp_reset_reason_t reset=esp_reset_reason();event_locked(reset==ESP_RST_TASK_WDT||reset==ESP_RST_INT_WDT||reset==ESP_RST_WDT?"boot_watchdog":reset==ESP_RST_BROWNOUT?"boot_brownout":"boot");UNLOCK();

    uint32_t random[4]; esp_fill_random(random,sizeof(random));

    snprintf(csrf_token,sizeof(csrf_token),"%08lx%08lx%08lx%08lx",(unsigned long)random[0],(unsigned long)random[1],(unsigned long)random[2],(unsigned long)random[3]);

    ESP_LOGI(TAG,"Wallbox EMS v%s starting; outputs blocked until commissioned",VERSION);

    /* Make the web UI reachable before optional LED and external hardware. AP-only

       operation must still permit local metering and manual control. */

    start_wifi(); start_web();

    status_led_setup();



    wallbox_ready=settings.shell_rs485_interface==1 || init_uart(WALLBOX_UART,settings.wallbox_tx_pin,settings.wallbox_rx_pin,settings.wallbox_rts_pin,9600,1);

    meter_ready=network_wallbox_meter() || settings.meter_rs485_interface==1 || init_uart(XEMEX_UART,settings.xemex_tx_pin,settings.xemex_rx_pin,settings.xemex_rts_pin,settings.meter_baud,settings.meter_format);
    house_bus_ready=!serial_house_meter() || settings.house_rs485_interface==1 || init_uart(HOUSE_UART,settings.house_tx_pin,settings.house_rx_pin,settings.house_rts_pin,settings.house_meter_baud,settings.house_meter_format);

    if(wallbox_ready && xTaskCreate(wallbox_task,"wallbox",4096,NULL,10,NULL)!=pdPASS) wallbox_ready=false;

    if(meter_ready && xTaskCreate(meter_task,"meter",4096,NULL,9,NULL)!=pdPASS) meter_ready=false;


    if(phase_mqtt_remote()&&settings.phase_switch_enabled&&
       xTaskCreate(phase_mqtt_task,"phase_output",settings.phase_output_transport?6144:4096,NULL,6,NULL)!=pdPASS){phase_state=PHASE_FAULT;phase_gpio_ready=false;}
    if(phase_shelly_remote()&&settings.phase_switch_enabled &&
       xTaskCreate(phase_shelly_task,"phase_shelly",6144,NULL,6,NULL)!=pdPASS){phase_state=PHASE_FAULT;phase_gpio_ready=false;}
    huawei_io=xSemaphoreCreateMutex();
    if(xTaskCreate(shell_limits_task,"shell_limits",6144,NULL,4,NULL)!=pdPASS)ESP_LOGE(TAG,"Shell limits task failed");
    if(huawei_io)xTaskCreate(huawei_task,"huawei",4096,NULL,6,NULL);
    if(xTaskCreate(house_meter_task,"house_meter",6144,NULL,6,NULL)!=pdPASS) ESP_LOGE(TAG,"House meter task failed");

    reset_clear_at=now_ms()+15000;

    if(xTaskCreate(status_task,"status",6144,NULL,5,NULL)!=pdPASS) abort();
    if(xTaskCreate(boot_check_task,"boot_check",3072,NULL,4,NULL)!=pdPASS)abort();

    ESP_LOGI(TAG,"Ready; setup AP %s is available when no home network is configured or after 5 minutes offline",SETUP_SSID);

}
