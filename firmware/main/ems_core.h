#pragma once
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include "expert_params.h"

#define EMS_DAYS 30
#define EMS_HISTORY 120
#define EMS_SOURCES 2
#define EMS_METER_TTL (ems_param_ms(EP_METER_TTL))
#define EMS_NETWORK_METER_TTL (ems_param_ms(EP_NET_METER_TTL))
#define EMS_INPUT_TTL (ems_param_ms(EP_INPUT_TTL))
#define EMS_HOUSE_TTL (ems_param_ms(EP_HOUSE_TTL))
#define EMS_NETWORK_HOUSE_TTL (ems_param_ms(EP_NET_HOUSE_TTL))
#define EMS_GRID_FALLBACK_W (ems_param(EP_GRID_FALLBACK))
#define EMS_CHARGE_STOP_CONFIRM_MS (ems_param_ms(EP_STOP_CONFIRM))
#define EMS_CHARGE_STOP_LIMIT 5
#define EMS_CHARGE_STOP_WINDOW_MS (ems_param_ms(EP_STOP_WINDOW))
#define EMS_SETTINGS_VERSION 33
#define EMS_MIN_CHARGE_A 6.0f
#define EMS_BATTERY_CAPACITY_KWH 20.0f
#define EMS_BATTERY_DISCHARGE_MAX_W 4000.0f
#define EMS_BATTERY_BUFFER_MS (ems_param_ms(EP_BAT_BUFFER))
#define EMS_BATTERY_BUFFER_RECOVERY_MS (ems_param_ms(EP_BAT_RECOVERY))
#define EMS_PV_START_MS (ems_param_ms(EP_PV_START))
#define EMS_BATTERY_RESPONSE_MS 45000

typedef enum { MODE_OFF, MODE_MANUAL, MODE_GRID_LIMIT, MODE_PV } control_mode_t;
typedef struct {
    uint32_t version;
    char wifi_ssid[33], wifi_password[65], mqtt_uri[128], mqtt_username[65];
    char mqtt_password[65], mqtt_prefix[64];
    int wallbox_tx_pin, wallbox_rx_pin, wallbox_rts_pin;
    int xemex_tx_pin, xemex_rx_pin, xemex_rts_pin;
    uint8_t wallbox_address, xemex_address;
    float grid_limit_a, max_charge_a, min_charge_a;
    control_mode_t mode;
    bool enabled;
    float manual_current_a, pv_surplus_a;
    /* The prefix above is the on-device v1 configuration layout. */
    float nominal_v, power_factor, price_kwh;
    bool control_verified; /* Retired UI acknowledgement; no control interlock. */
    /* Version 3 fields. Positive house power means grid import, negative means export. */
    bool zero_feed_enabled;
    char house_meter_type[20], house_meter_host[64];
    float zero_reserve_w;
    uint8_t xemex_coils;
    /* Version 5: physical Wallbox meter and network house meter are independent. */
    char wallbox_meter_type[20];
    int meter_baud;
    uint8_t meter_format; /* 0: 8N1, 1: 8E1, 2: 8O1, 3: 8N2 */
    char house_power_path[96];
    bool mqtt_state_json;
    /* Version 6: optional PV-only battery reserve, fed by live MQTT inputs. */
    bool battery_protect;
    float battery_reserve_soc;
    /* Version 7: measured excess above the former controller setpoint. */
    float current_offset_a;
    float solar_price_kwh;
    /* Version 9: optional experimental controls. Keep the second byte reserved
       so existing settings blobs remain binary-compatible after removing the
       ineffective Solis discharge-limit experiment. */
    bool phase_switch_enabled, reserved_solis_discharge;
    uint8_t charge_phases, manual_phases;
    /* Version 10: optional expert hardware and display integrations. */
    bool expert_mode, vehicle_soc_enabled;
    bool external_mode_input_enabled, evu_input_enabled;
    int external_mode_input_pin, evu_input_pin;
    float evu_limit_a;
    bool grid_guard_enabled;
    int house_tx_pin, house_rx_pin, house_rts_pin;
    uint8_t house_address;
    int house_meter_baud;
    uint8_t house_meter_format, house_xemex_coils;
    /* Retired v10 ADC bytes: retained only for migration of saved settings. */
    bool ads1115_enabled;
    int ads_sda_pin, ads_scl_pin;
    uint8_t ads_wallbox_address, ads_house_address, ads_coil_rating_a;
    float ads_full_scale_v;
    /* Version 11: hardware inventory and optional relay board. */
    uint8_t mqtt_input_source; /* 0 ioBroker, 1 Home Assistant; same local MQTT input topics */
    bool relay_board_enabled, relay_active_low, mqtt_enabled, pv_display_enabled;
    int relay1_pin, relay2_pin;
    uint8_t relay1_mode, relay2_mode; /* 0 off, 1 charging signal, 2 fault signal, 3 phase contactor (relay 1 only) */
    /* Retired HTTP-wallbox experiment. Bytes remain reserved so old saved
       configurations can be migrated without moving later fields. */
    bool reserved_http_meter_enabled;
    char reserved_http_meter_host[64];
    /* Version 13: separate, user-configurable battery allowances. */
    float battery_cloud_limit_w;
    float battery_assist_limit_w;
    bool homeassistant_enabled;
    uint8_t reserved_feedback_source;
    /* Retired Shelly HTTP password bytes, kept for settings migration. */
    char house_meter_password[65];
    /* Version 16: optional Shelly at the Wallbox feeder. Password bytes are
       retired because Shelly meters now use unauthenticated Modbus TCP. */
    char wallbox_meter_host[64], wallbox_meter_password[65];
    /* Version 18: the optional planner is hidden and inert by default. */
    bool charge_plan_enabled;
    /* Version 19: optional direct Huawei readings; independent of MQTT. */
    bool huawei_enabled, huawei_battery, huawei_pv;
    char huawei_host[64];
    uint8_t huawei_unit_id;
    /* Version 20: fixed wiring, independent of the optional phase contactor. */
    uint8_t fixed_charge_phases;
    /* Version 21: Shell LAN supplies configuration limits, never metering. */
    bool shell_limits_auto;
    char shell_setup_host[64];
    /* Version 22: auxiliary contact optional; true preserves prior interlock. */
    bool phase_feedback_enabled;
    /* Version 23: support isolated NO and NC position contacts. */
    bool phase_feedback_closed_is_single;
    /* Version 24: optional overview status line; presentation only. */
    bool control_status_visible;
    /* Version 25: basic manual/MQTT operation without optional integrations. */
    bool basic_mode;
    bool pv_allocation_enabled;
    uint8_t pv_priority; /* 0 house first, 1 car first, 2 proportional */
    float pv_house_priority_w, pv_car_priority_w;
    /* Version 26: read-only native OpenDTU-OnBattery MQTT input. */
    char opendtu_prefix[96], opendtu_pv_topic[160], opendtu_pv_valid_topic[160];
    bool opendtu_current_positive_discharge;

    /* Version 27: transparent RTU-over-TCP gateways, independent per bus. */
    uint8_t shell_rs485_interface, meter_rs485_interface, house_rs485_interface;
    char shell_rs485_host[64], meter_rs485_host[64], house_rs485_host[64];
    bool evcc_feature_enabled;

    /* Version 28: ABB E290/E299 via Shelly Pro 1; default keeps GPIO hardware. */
    uint8_t phase_actuator_type; /* 0 GPIO, 1 Shelly RPC, 2 expert output, 3 Shelly Modbus */
    char phase_shelly_host[64],phase_shelly_password[65];
    /* Version 29: outgoing absolute boolean command; expert opt-in only. */
    bool phase_mqtt_output_enabled;
    char phase_mqtt_topic[128]; /* empty selects <prefix>/output/single_phase */
    /* Version 30: NO contactor true = three phases; default NC true = single. */
    bool phase_mqtt_true_three_phase;
    /* Version 31: absolute output over MQTT, Shelly Modbus or unauthenticated RPC. */
    uint8_t phase_output_transport; /* 0 MQTT, 1 Modbus TCP, 2 HTTP/RPC */
    uint8_t phase_output_switch_id; /* 0 normal switch, 100 Pro 3EM output add-on */
    char phase_output_host[64];

    /* Version 32: mandatory normal feedback, optional complementary contact,
       compatible Shelly profiles and explicit authenticated expert RPC. */
    uint8_t phase_feedback_count; /* 1 contact, 2 complementary contacts */
    uint8_t phase_shelly_model; /* 0..12: supported 230-V Shelly profile */
    bool phase_shelly_pulse; /* bounded bistable pulse; otherwise absolute state */
    bool phase_shelly_true_three_phase;
    char phase_output_password[65]; /* used only by expert transport 3 */

    /* Version 33: expert GPIO assignment, applied only at boot. */
    int phase_feedback_pin, phase_feedback_second_pin, status_led_pin;

} settings_t;
#include "gpio_map.h"
bool phase_three_phase_meter(const settings_t *settings);
unsigned phase_measured_currents(const settings_t *settings);
bool phase_switch_meter_supported(const settings_t *settings);
bool phase_measured_zero(const settings_t *settings,const float actual[3],float limit);
bool phase_mqtt_output_topic(const settings_t *settings,char *out,size_t capacity);
bool phase_mqtt_output_value(const settings_t *settings,unsigned phases);
bool phase_output_settings_supported(const settings_t *settings);

int relay_output_level(bool active_low, bool energized);
void settings_fixed_pins(settings_t *settings);
void settings_basic_mode(settings_t *settings);
bool rs485_endpoint_valid(const char *text, size_t capacity);

typedef struct {
    int64_t since,last_sample;
    float target,lowest,highest;
    double sum;
    unsigned samples,adjustments;
} current_calibration_t;
bool current_calibration_step(current_calibration_t *c,settings_t *s,bool permitted,
    float target,const float actual[3],int64_t sample_at,int64_t now);

typedef struct {
    uint32_t date;
    double wh[EMS_SOURCES];
    uint64_t covered_ms[EMS_SOURCES];
    uint64_t charging_ms;
} energy_day_t;
typedef struct {
    uint32_t version;
    double total_wh[EMS_SOURCES], undated_wh[EMS_SOURCES];
    energy_day_t days[EMS_DAYS];
} energy_store_t;
typedef struct {
    int64_t minute;
    double wh[EMS_SOURCES];
    uint32_t covered_ms[EMS_SOURCES];
} energy_point_t;
typedef struct {
    energy_store_t store;
    double boot_wh[EMS_SOURCES];
    energy_point_t history[EMS_HISTORY];
    int64_t last_ms, last_epoch_ms, expires[EMS_SOURCES];
    double previous_w[EMS_SOURCES];
    uint32_t last_date;
    bool started;
} energy_t;

void settings_defaults(settings_t *s);
bool settings_valid(const settings_t *s);
bool settings_decode(const void *blob, size_t length, settings_t *out);
bool settings_apply_live(settings_t *runtime, const settings_t *before, const settings_t *after);
bool reconstruct_currents(float values[3], unsigned coils);
bool wallbox_inactive_phase_current(const settings_t *settings,const float actual[3]);
typedef struct { uint16_t current_register, power_register; unsigned phases; } sdm_profile_t;
const sdm_profile_t *sdm_profile(const char *type);
bool decode_sdm_reading(const sdm_profile_t *profile, uint8_t address,
    const uint8_t *currents, size_t current_length, const uint8_t *power, size_t power_length,
    float out[3], float *total_w);
bool decode_sdm(const uint8_t *frame, size_t length, uint8_t address, float *out, unsigned count, float lo, float hi);
float solar_current(const settings_t *s, const float actual[3], float grid_w);
float pv_allocation_grid(const settings_t *s, const float actual[3], float grid_w,
    float charge_w, float discharge_w, float soc, bool battery_fresh);
float pv_start_threshold(const settings_t *s);
float estimated_charge_power(const settings_t *s, const float currents[3]);
float battery_solar_current(const settings_t *s, const float actual[3], float grid_w,
                            float soc, bool battery_fresh, float discharge_w, bool discharge_fresh);
float battery_assisted_current(const settings_t *s, const float actual[3], float grid_w,
                              float soc, bool battery_fresh, float discharge_w, bool discharge_fresh,
                              bool allow_battery, bool charging);
typedef struct {
    int64_t started_at, recovery_at, cloud_response_at, assist_response_at, last_at;
    bool started, active, assist_active;
    uint32_t remaining_ms;
} battery_buffer_t;
typedef struct { bool fresh, conflicting; float net_w, charge_w, discharge_w; } battery_power_t;
battery_power_t battery_power_normalize(float charge_w,float discharge_w,int64_t charge_at,int64_t discharge_at,int64_t now,int64_t ttl);
float battery_support_current(battery_buffer_t *buffer,const settings_t *s,const float actual[3],float grid_w,float soc,
    bool soc_fresh,float discharge_w,bool power_fresh,bool cloud_enabled,bool assist_enabled,bool charging,float minimum_a,int64_t now);
float battery_cloud_current(battery_buffer_t *buffer, const settings_t *s,
                            const float actual[3], float grid_w, float soc,
                            bool battery_fresh, float discharge_w, bool discharge_fresh,
                            bool enabled, bool charging, float minimum_a, int64_t now);
float pv_ramp_available(float available, float current_target, const float actual[3]);
float pv_ramp_available_phases(float available, float current_target, const float actual[3], unsigned phases);
typedef enum { PV_BLOCKED, PV_WAITING, PV_STARTING, PV_RUNNING, PV_STOPPING, PV_COOLDOWN } pv_phase_t;
typedef struct {
    bool initialized, stopped;
    float target_a;
    int64_t last_at, above_at, below_at, stopped_at, adjusted_at;
    pv_phase_t phase;
    uint32_t wait_ms;
} pv_control_t;
void pv_control_step(pv_control_t *control, bool permitted, float available_a,
                     float minimum_a, float maximum_a, int64_t now);
void pv_control_step_threshold(pv_control_t *control, bool permitted, float available_a,
    float minimum_a, float maximum_a, float start_a, int64_t now);
void pv_control_step_timed(pv_control_t *control,bool permitted,float available_a,float minimum_a,float maximum_a,float start_a,int64_t start_delay_ms,int64_t now);
bool pv_control_takeover(pv_control_t *control, bool permitted, float previous_target,
    const float actual[3], unsigned phases, float minimum, float maximum, int64_t now);
typedef struct {
    uint32_t version,flags,cloud_ms,cooldown_ms;
    int64_t cloud_epoch_ms,cooldown_epoch_ms;
    uint32_t checksum;
} control_memory_t;
void control_memory_capture(control_memory_t *memory,const battery_buffer_t *buffer,const pv_control_t *pv,bool cloud_enabled,bool assist_enabled,int64_t now,int64_t epoch_ms);
bool control_memory_valid(const control_memory_t *memory,size_t size);
void control_memory_restore(const control_memory_t *memory,battery_buffer_t *buffer,pv_control_t *pv,int64_t now,int64_t epoch_ms);
bool parse_number(const char *text, double lo, double hi, double *out);
bool parse_mode(const char *text, control_mode_t *out);
const char *mode_name(control_mode_t mode);
bool fresh(int64_t now, int64_t last, int64_t ttl);
bool phase_option_change_allowed(bool was_enabled, bool requested_enabled,
                                 unsigned active_phases, bool phase_ready,
                                 bool meter_fresh, const float actual[3]);
bool phase_setup_change_allowed(bool was_enabled,bool requested_enabled,
                                unsigned active_phases,bool phase_ready,bool charging,
                                bool has_measurements,bool meter_fresh,const float actual[3]);
bool fixed_wiring_change_allowed(unsigned before,unsigned after,bool charging,
                                 bool phase_ready,bool has_measurements,
                                 bool meter_fresh,const float actual[3]);
bool hardware_config_change_allowed(bool charging,bool quiescent,bool has_measurements,
                                    bool meter_fresh,const float actual[3]);
typedef enum { CONFIG_GATE_OK, CONFIG_GATE_CHARGING, CONFIG_GATE_MOVING,
               CONFIG_GATE_STALE, CONFIG_GATE_INVALID, CONFIG_GATE_CURRENT } config_gate_t;
config_gate_t hardware_config_gate(bool charging,bool quiescent,bool has_measurements,
                                  bool meter_fresh,const float actual[3]);
typedef enum { EMS_WIFI_AP, EMS_WIFI_STA, EMS_WIFI_AP_STA } ems_wifi_mode_t;
ems_wifi_mode_t wifi_recovery_mode(bool configured, bool online, bool forced_ap, int64_t now, int64_t lost_at);
float control_target(const settings_t *s, bool meter_ok, bool feedback_ok, bool pv_ok);
bool house_phase_currents_ready(bool supported, const float house[3],
                                int64_t now, int64_t measured_at);
float grid_guard_target(const settings_t *s, float requested, bool house_ready);
float charge_plan_maximum_kw(const settings_t *s, bool house_ready, bool evu_active);
bool settings_shell_pin_available(const settings_t *s, int pin, bool tx);
void grid_guard_report(const settings_t *s, bool house_ready,
                       const float house[3], float reported[3]);
bool phase_feedback_matches(bool required, unsigned observed, unsigned expected);
unsigned phase_feedback_position(bool contact_closed, bool closed_is_single);
unsigned phase_feedback_contacts(bool first_closed,bool second_closed,unsigned count,bool closed_is_single);
bool phase_motion_complete(bool required, unsigned observed, unsigned expected, int64_t elapsed_ms);
bool phase_fault_reset_allowed(bool faulted, bool gpio_ready, bool feedback_required,
                               unsigned observed, bool relay_on, bool measurement_fresh,
                               const float actual[3]);
void control_report(const settings_t *s, const float actual[3], float target,
                    float house_power_w, bool house_power_ok, float out[3]);
typedef struct {
    float reported[3];
    int64_t last_ms;
    bool active;
} manual_report_filter_t;
void manual_report_smooth(manual_report_filter_t *filter,const settings_t *settings,
                          float target,int64_t now,float report[3]);
typedef struct {
    bool seen_charging, latched;
    int64_t low_since, charging_since, last_at, retry_until, stops[10];
    unsigned stop_count;
} charge_guard_t;
void charge_guard_step(charge_guard_t *guard, bool requested, bool feedback_ok,
                       const float actual[3], float minimum_a, int64_t now);
bool charge_guard_blocked(const charge_guard_t *guard, int64_t now);
uint16_t modbus_crc(const uint8_t *data, size_t length);
void append_crc(uint8_t *data, size_t length);
bool valid_frame(const uint8_t *data, size_t length);
bool decode_meter(const uint8_t *frame, size_t length, uint8_t address, float out[3]);
size_t modbus_reply(const uint8_t *request, size_t len, uint8_t *reply,
                   const settings_t *settings, const float currents[3]);
void energy_step(energy_t *e, int64_t now, int64_t epoch_ms, uint32_t date,
                 int64_t day_start_epoch_ms, const double power[2], const int64_t expires[2]);
bool energy_store_valid(const energy_store_t *store);
bool energy_store_decode(const void *blob, size_t length, energy_store_t *out);
