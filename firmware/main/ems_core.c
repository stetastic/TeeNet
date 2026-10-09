#include "sdkconfig.h"
#include "ems_core.h"
#include <math.h>
#include <string.h>
#include <stdlib.h>
#include <errno.h>
#include <ctype.h>

void settings_defaults(settings_t *s) {
    memset(s, 0, sizeof(*s));
    s->version = EMS_SETTINGS_VERSION;
    s->huawei_unit_id=1;
    strcpy(s->mqtt_prefix, "wallbox-ems");
    s->wallbox_tx_pin=17; s->wallbox_rx_pin=18; s->wallbox_rts_pin=-1;
    s->xemex_tx_pin=4; s->xemex_rx_pin=5; s->xemex_rts_pin=-1;
    s->wallbox_address=s->xemex_address=1;
    s->grid_limit_a=32; s->max_charge_a=16; s->min_charge_a=8.7f;
    s->manual_current_a=8.7f; s->nominal_v=230; s->power_factor=1; s->price_kwh=0.25f; s->solar_price_kwh=0.08f;
    strcpy(s->house_meter_type,"tasmota"); strcpy(s->house_meter_host,"192.168.1.77"); s->zero_reserve_w=0;
    s->battery_reserve_soc=50;
    s->current_offset_a=0.8f;
    s->xemex_coils=1;
    strcpy(s->wallbox_meter_type,"xemex"); s->meter_baud=9600; s->meter_format=1;
    s->charge_phases=3; s->manual_phases=3; s->fixed_charge_phases=3;
    s->phase_feedback_enabled=true;
    s->phase_feedback_closed_is_single=true;
    s->control_status_visible=true;
    s->vehicle_soc_enabled=true;
    s->mqtt_enabled=true; s->pv_display_enabled=true;
    s->relay1_pin=12; s->relay2_pin=14;
    s->battery_cloud_limit_w=4000; s->battery_assist_limit_w=4000;
    s->pv_house_priority_w=4000; s->pv_car_priority_w=6000;
    strcpy(s->opendtu_prefix,"opendtu");
    #if defined(CONFIG_TEE_DISABLE_BETRIEBSARTKONTAKT)
        // AtomS3 Lite Anpassung:
        s->external_mode_input_pin = -1; // Deaktiviert (kein Konflikt auf GPIO 6)
        s->external_mode_input_enabled = false; 
    #else
        // Standard 16MB Version:
        s->external_mode_input_pin = 6; 
    #endif
    
    s->evu_input_pin=7; s->evu_limit_a=0;
    s->house_tx_pin=43; s->house_rx_pin=44; s->house_rts_pin=-1;
    s->house_address=1; s->house_meter_baud=9600; s->house_meter_format=0; s->house_xemex_coils=3;
    s->ads_sda_pin=8; s->ads_scl_pin=9; s->ads_wallbox_address=0x48; s->ads_house_address=0x49;
    s->ads_coil_rating_a=50; s->ads_full_scale_v=0.333f;
}
static bool range(double n, double lo, double hi) { return isfinite(n) && n>=lo && n<=hi; }
void settings_basic_mode(settings_t *s) {
    if(!s->mqtt_enabled)s->mqtt_input_source=0;
    if(!s->battery_protect || !s->zero_feed_enabled)s->pv_allocation_enabled=false;
    if(!s->basic_mode)return;
    s->expert_mode=false; s->pv_allocation_enabled=false;
    s->zero_feed_enabled=s->battery_protect=s->vehicle_soc_enabled=s->pv_display_enabled=false;
    s->charge_plan_enabled=s->external_mode_input_enabled=s->evu_input_enabled=s->grid_guard_enabled=false;
    s->phase_switch_enabled=s->relay_board_enabled=false;
    s->huawei_enabled=s->huawei_battery=s->huawei_pv=false;
    s->mqtt_input_source=0;
    /* An inactive Huawei/serial house meter must not reserve pins or require a host. */
    strcpy(s->house_meter_type,"tasmota");
    if(s->mode!=MODE_OFF && s->mode!=MODE_MANUAL){s->mode=MODE_OFF;s->enabled=false;}
}
void settings_fixed_pins(settings_t *s) {
    #if defined(CONFIG_TEE_DISABLE_BETRIEBSARTKONTAKT)
        // --- 8MB ATOMS3 LITE LOGIK ---
        if(s->xemex_tx_pin != 4 || s->xemex_rx_pin != 5 ||
           s->house_tx_pin != 43 || s->house_rx_pin != 44 || 
           s->evu_input_pin != 7 || s->relay1_pin != 12 || s->relay2_pin != 14) {
            s->control_verified = false;
        }
        s->external_mode_input_pin = -1; // Dauerhaft deaktiviert für AtomS3
        s->external_mode_input_enabled = false;
    #else
        // --- STANDARD 16MB LOGIK ---
        if(s->xemex_tx_pin != 4 || s->xemex_rx_pin != 5 ||
           s->house_tx_pin != 43 || s->house_rx_pin != 44 || s->external_mode_input_pin != 6 ||
           s->evu_input_pin != 7 || s->relay1_pin != 12 || s->relay2_pin != 14) {
            s->control_verified = false;
        }
        s->external_mode_input_pin = 6;
    #endif

    // Gemeinsame Pins zurückschreiben
    s->xemex_tx_pin = 4; s->xemex_rx_pin = 5;
    s->house_tx_pin = 43; s->house_rx_pin = 44;
    s->evu_input_pin = 7;
    s->relay1_pin = 12; s->relay2_pin = 14;
    s->wallbox_rts_pin = s->xemex_rts_pin = s->house_rts_pin = -1;
}
static bool pin_valid(int pin) { return pin==1 || pin==2 || (pin>=4 && pin<=18) || pin==21 || (pin>=38 && pin<=44) || pin==47; }
bool settings_shell_pin_available(const settings_t *s,int pin,bool tx) {
    bool wallbox_serial=strcmp(s->wallbox_meter_type,"shelly_gen2") && strcmp(s->wallbox_meter_type,"shelly_em1") && strcmp(s->wallbox_meter_type,"em24_tcp");
    if(!pin_valid(pin) || pin==(tx?s->wallbox_rx_pin:s->wallbox_tx_pin) ||
       (wallbox_serial && (pin==s->xemex_tx_pin || pin==s->xemex_rx_pin))) return false;
    bool serial=!strcmp(s->house_meter_type,"xemex") || sdm_profile(s->house_meter_type)!=NULL;
    return !(serial && (pin==s->house_tx_pin || pin==s->house_rx_pin)) &&
        !(s->external_mode_input_enabled && pin==s->external_mode_input_pin) &&
        !(s->evu_input_enabled && pin==s->evu_input_pin) &&
        !(s->relay_board_enabled && (pin==s->relay1_pin || pin==s->relay2_pin)) &&
        !(s->phase_switch_enabled && s->phase_feedback_enabled && pin==13);
}
static bool mqtt_topic_valid(const char *text,size_t size,bool required) {
    if(!memchr(text,0,size) || (required&&!text[0]))return false;
    for(const unsigned char *p=(const unsigned char *)text;*p;p++)if(*p<32||*p==127||*p=='#'||*p=='+')return false;
    return true;
}
bool settings_valid(const settings_t *s) {
    if(!mqtt_topic_valid(s->opendtu_prefix,sizeof(s->opendtu_prefix),s->mqtt_input_source==2) ||
       !mqtt_topic_valid(s->opendtu_pv_topic,sizeof(s->opendtu_pv_topic),s->mqtt_input_source==2&&s->pv_display_enabled) ||
       !mqtt_topic_valid(s->opendtu_pv_valid_topic,sizeof(s->opendtu_pv_valid_topic),false))return false;
    if(s->mqtt_input_source==2 && (s->opendtu_prefix[strlen(s->opendtu_prefix)-1]=='/' || !strcmp(s->opendtu_prefix,s->mqtt_prefix)))return false;

    if(!memchr(s->house_meter_type,0,sizeof(s->house_meter_type)) ||
       !memchr(s->wallbox_meter_type,0,sizeof(s->wallbox_meter_type)) ||
       !memchr(s->house_meter_password,0,sizeof(s->house_meter_password)) ||
       !memchr(s->wallbox_meter_host,0,sizeof(s->wallbox_meter_host)) ||
       !memchr(s->wallbox_meter_password,0,sizeof(s->wallbox_meter_password))) return false;
    bool house_serial=!strcmp(s->house_meter_type,"xemex") || sdm_profile(s->house_meter_type)!=NULL;
    bool wallbox_serial=strcmp(s->wallbox_meter_type,"shelly_gen2") && strcmp(s->wallbox_meter_type,"shelly_em1") && strcmp(s->wallbox_meter_type,"em24_tcp");
    if(!strcmp(s->house_meter_type,"sdm230") || !strcmp(s->house_meter_type,"sdm120") ||
       (!strcmp(s->house_meter_type,"xemex") && s->house_xemex_coils!=3) || s->xemex_coils==2)return false;
    const int pins[]={s->wallbox_tx_pin,s->wallbox_rx_pin,s->wallbox_rts_pin,
        s->xemex_tx_pin,s->xemex_rx_pin,s->xemex_rts_pin,s->house_tx_pin,s->house_rx_pin,s->house_rts_pin,
        s->external_mode_input_pin,s->evu_input_pin,s->relay1_pin,s->relay2_pin,13};
    const bool used[]={true,true,s->wallbox_rts_pin!=-1,
        wallbox_serial,wallbox_serial,wallbox_serial && s->xemex_rts_pin!=-1,
        house_serial,house_serial,house_serial && s->house_rts_pin!=-1,
        s->external_mode_input_enabled,s->evu_input_enabled,
        s->relay_board_enabled,s->relay_board_enabled,s->phase_switch_enabled && s->phase_feedback_enabled};
    for(unsigned i=0;i<sizeof(pins)/sizeof(pins[0]);i++) if(used[i]) {
        if(!pin_valid(pins[i])) return false;
        for(unsigned j=0;j<i;j++) if(used[j] && pins[i]==pins[j]) return false;
    }
    if(s->ads1115_enabled) return false; /* retired input cannot be reactivated */
    if(s->mqtt_input_source>2 || s->relay1_mode>3 || s->relay2_mode>2 ||
       (s->phase_switch_enabled && (!s->relay_board_enabled || s->relay1_mode!=3))) return false;
    if(!s->mqtt_enabled && (s->vehicle_soc_enabled || s->homeassistant_enabled ||
       (s->pv_display_enabled && !(s->huawei_enabled && s->huawei_pv)) ||
       (s->battery_protect && !(s->huawei_enabled && s->huawei_battery))))return false;
    if(!memchr(s->huawei_host,0,sizeof(s->huawei_host)) || s->huawei_unit_id>247)return false;
    if(s->huawei_enabled && !s->huawei_host[0])return false;
    for(const unsigned char *p=(const unsigned char *)s->huawei_host;*p;p++)if(!isalnum(*p)&&*p!='.'&&*p!='-'&&*p!=':')return false;
    if(!strcmp(s->house_meter_type,"huawei") && !s->huawei_enabled)return false;

    const char *strings[]={s->wifi_ssid,s->wifi_password,s->mqtt_uri,s->mqtt_username,s->mqtt_password,s->mqtt_prefix};
    const size_t sizes[]={33,65,128,65,65,64};
    for (int i=0;i<6;i++) if (!memchr(strings[i],0,sizes[i])) return false;
    size_t pw=strlen(s->wifi_password);
    if (pw && (pw<8 || pw>63)) return false;
    if (s->mqtt_uri[0] && strncmp(s->mqtt_uri,"mqtt://",7) && strncmp(s->mqtt_uri,"mqtts://",8)) return false;
    if (s->mqtt_uri[0] && !strstr(s->mqtt_uri,"://")[3]) return false;
    /* Credentials are separate so config responses never expose URI passwords. */
    if (strchr(s->mqtt_uri,'@')) return false;
    if (!s->mqtt_prefix[0] || s->mqtt_prefix[0]=='/' || s->mqtt_prefix[strlen(s->mqtt_prefix)-1]=='/') return false;
    for (const unsigned char *p=(const unsigned char *)s->mqtt_prefix;*p;p++)
        if (!isalnum(*p) && *p!='/' && *p!='_' && *p!='-') return false;
    if(!memchr(s->house_meter_type,0,sizeof(s->house_meter_type)) || !memchr(s->house_meter_host,0,sizeof(s->house_meter_host))) return false;
    if(strcmp(s->house_meter_type,"tasmota") && strcmp(s->house_meter_type,"shelly_gen2") && strcmp(s->house_meter_type,"shelly_em1") &&
       strcmp(s->house_meter_type,"xemex") && strcmp(s->house_meter_type,"huawei") && strcmp(s->house_meter_type,"em24_tcp") && !sdm_profile(s->house_meter_type)) return false;
    if(!memchr(s->wallbox_meter_type,0,sizeof(s->wallbox_meter_type)) || !memchr(s->house_power_path,0,sizeof(s->house_power_path))) return false;
    bool network_wallbox=!strcmp(s->wallbox_meter_type,"shelly_gen2") || !strcmp(s->wallbox_meter_type,"shelly_em1") || !strcmp(s->wallbox_meter_type,"em24_tcp");
    if(strcmp(s->wallbox_meter_type,"xemex") && !sdm_profile(s->wallbox_meter_type) && !network_wallbox) return false;
    if(s->meter_baud!=1200 && s->meter_baud!=2400 && s->meter_baud!=4800 && s->meter_baud!=9600 && s->meter_baud!=19200 && s->meter_baud!=38400) return false;
    bool house_url=!strncmp(s->house_power_path,"http://",7) || !strncmp(s->house_power_path,"https://",8);
    bool network_house=!strcmp(s->house_meter_type,"tasmota") || !strcmp(s->house_meter_type,"shelly_gen2") || !strcmp(s->house_meter_type,"shelly_em1") || !strcmp(s->house_meter_type,"em24_tcp");
    if(!strcmp(s->house_meter_type,"em24_tcp") && !s->house_meter_host[0])return false;
    bool directional_house=network_house || !strcmp(s->house_meter_type,"huawei") || sdm_profile(s->house_meter_type)!=NULL;
    if(!memchr(s->shell_setup_host,0,sizeof(s->shell_setup_host)))return false;
    for(const unsigned char *p=(const unsigned char *)s->shell_setup_host;*p;p++)if(!isalnum(*p)&&*p!='.'&&*p!='-')return false;
    if(s->meter_format>3 || s->house_meter_format>3 || (s->zero_feed_enabled && (!directional_house || (network_house && !s->house_meter_host[0] && !house_url)))) return false;
    for(const unsigned char *p=(const unsigned char *)s->house_meter_host;*p;p++) if(!isalnum(*p) && *p!='.' && *p!='-' && *p!=':') return false;
    for(const unsigned char *p=(const unsigned char *)s->wallbox_meter_host;*p;p++) if(!isalnum(*p) && *p!='.' && *p!='-' && *p!=':') return false;
    return s->version==EMS_SETTINGS_VERSION && s->xemex_coils>=1 && s->xemex_coils<=3 && s->wallbox_address>=1 && s->wallbox_address<=247 &&
        s->xemex_address>=1 && s->xemex_address<=247 &&
        range(s->grid_limit_a,EMS_MIN_CHARGE_A,200) && range(s->max_charge_a,EMS_MIN_CHARGE_A,63) && s->max_charge_a<=s->grid_limit_a &&
        range(s->min_charge_a,EMS_MIN_CHARGE_A,s->max_charge_a) && range(s->manual_current_a,0,s->max_charge_a) &&
        range(s->pv_surplus_a,0,s->max_charge_a) && range(s->mode,MODE_OFF,MODE_PV) &&
        range(s->nominal_v,100,260) && range(s->power_factor,0.1,1) && range(s->price_kwh,0,10) && range(s->zero_reserve_w,-5000,5000) &&
        range(s->battery_reserve_soc,0,100) && (!s->battery_protect || (s->zero_feed_enabled && s->zero_reserve_w<=0)) &&
        s->pv_priority<=2 && range(s->pv_house_priority_w,0,30000) && range(s->pv_car_priority_w,500,22000) &&
        (!s->pv_allocation_enabled || (s->battery_protect && s->zero_feed_enabled && !s->basic_mode)) &&
        range(s->battery_cloud_limit_w,500,4000) && range(s->battery_assist_limit_w,500,4000) &&
        range(s->current_offset_a,0,2) && range(s->solar_price_kwh,0,10) && s->min_charge_a-s->current_offset_a>=EMS_MIN_CHARGE_A &&
        (s->fixed_charge_phases==1 || s->fixed_charge_phases==3) &&
        (!s->phase_switch_enabled || s->fixed_charge_phases==3) &&
        (s->fixed_charge_phases!=1 || s->max_charge_a<=16) &&
        (s->charge_phases==1 || s->charge_phases==3) && (s->manual_phases==1 || s->manual_phases==3) &&
        (!network_wallbox || s->wallbox_meter_host[0]) &&
        (!s->phase_switch_enabled || (s->max_charge_a<=16 && (strcmp(s->wallbox_meter_type,"xemex") || s->xemex_coils==1))) &&
        (!s->evu_input_enabled || s->evu_limit_a==0 || range(s->evu_limit_a,s->min_charge_a,s->max_charge_a)) &&
        s->house_address>=1 && s->house_address<=247 &&
        (s->house_meter_baud==1200 || s->house_meter_baud==2400 || s->house_meter_baud==4800 || s->house_meter_baud==9600 || s->house_meter_baud==19200 || s->house_meter_baud==38400) &&
        s->house_xemex_coils>=1 && s->house_xemex_coils<=3 &&
        (!s->grid_guard_enabled || ((!strcmp(s->house_meter_type,"xemex") && s->house_xemex_coils==3) ||
            !strcmp(s->house_meter_type,"sdm630") || !strcmp(s->house_meter_type,"sdm630mct") ||
            !strcmp(s->house_meter_type,"shelly_gen2") || !strcmp(s->house_meter_type,"em24_tcp") ||
            (!strcmp(s->house_meter_type,"huawei") && s->huawei_enabled)));
}
bool settings_decode(const void *blob,size_t length,settings_t *out) {
    if(!blob || length<4) return false;
    uint32_t version; memcpy(&version,blob,4);
    size_t prefix;
    switch(version) {
        case 1: prefix=offsetof(settings_t,nominal_v); break;
        case 2: prefix=offsetof(settings_t,control_verified)+sizeof(bool); break;
        case 3: prefix=offsetof(settings_t,zero_reserve_w)+sizeof(float); break;
        case 4: prefix=offsetof(settings_t,xemex_coils)+sizeof(uint8_t); break;
        case 5: prefix=offsetof(settings_t,mqtt_state_json)+sizeof(bool); break;
        case 6: prefix=offsetof(settings_t,battery_reserve_soc)+sizeof(float); break;
        case 7: prefix=offsetof(settings_t,current_offset_a)+sizeof(float); break;
        case 8: prefix=offsetof(settings_t,phase_switch_enabled); break;
        case 9: prefix=offsetof(settings_t,expert_mode); break;
        case 10: prefix=offsetof(settings_t,mqtt_input_source); break;
        case 11: prefix=offsetof(settings_t,reserved_http_meter_enabled); break;
        case 12: prefix=offsetof(settings_t,battery_cloud_limit_w); break;
        case 13: prefix=offsetof(settings_t,reserved_feedback_source); break;
        case 14: prefix=offsetof(settings_t,house_meter_password); break;
        case 15: prefix=offsetof(settings_t,wallbox_meter_host); break;
        case 16: case 17: prefix=offsetof(settings_t,charge_plan_enabled); break;
        case 18: prefix=offsetof(settings_t,huawei_enabled); break;
        case 19: prefix=offsetof(settings_t,fixed_charge_phases); break;
        case 20: prefix=offsetof(settings_t,shell_limits_auto); break;
        case 21: prefix=offsetof(settings_t,phase_feedback_enabled); break;
        case 22: prefix=offsetof(settings_t,phase_feedback_closed_is_single); break;
        case 23: prefix=offsetof(settings_t,control_status_visible); break;
        case 24: prefix=offsetof(settings_t,basic_mode); break;
        case 25: prefix=offsetof(settings_t,opendtu_prefix); break;
        case EMS_SETTINGS_VERSION: prefix=sizeof(settings_t); break;
        default: return false;
    }
    /* Old structs include trailing alignment padding; never copy it over new defaults. */
    if(length!=((prefix+3)&~(size_t)3)) return false;
    settings_t next; settings_defaults(&next); memcpy(&next,blob,prefix);
    /* Keep the selected mode and setpoints visible after a restart, but never
       resume charging automatically. The user must enable it explicitly. */
    next.version=EMS_SETTINGS_VERSION; next.enabled=false;
    next.charge_phases=next.phase_switch_enabled?3:next.fixed_charge_phases; next.manual_phases=next.charge_phases;
    if(version<8){next.price_kwh=.25f;next.solar_price_kwh=.08f;}
    if(version<7) {
        /* Translate old internal setpoints into desired measured amperes once.
           8 A is a provisional operating floor from this installation's reports. */
        next.current_offset_a=fminf(0.8f,fmaxf(0,next.max_charge_a-EMS_MIN_CHARGE_A));
        next.min_charge_a=fminf(next.max_charge_a,fmaxf(8,next.min_charge_a+next.current_offset_a));
        if(next.manual_current_a>0) next.manual_current_a=fminf(next.max_charge_a,next.manual_current_a+next.current_offset_a);
        next.pv_surplus_a=0; /* Live surplus is recalculated from fresh inputs. */
    }
    /* The measured, commissioned limit is configurable. Do not overwrite it
       with an assumed Shell minimum during loading. */
    if(!memchr(next.house_meter_type,0,sizeof(next.house_meter_type))) return false;
    if(version<5) {
        next.control_verified=false; /* Measurement role has changed; commissioning must be repeated. */
        if(!strcmp(next.house_meter_type,"sdm630") || !strcmp(next.house_meter_type,"sdm320")) {
            /* User clarified that the previously named SDM320 was an SDM230. */
            strcpy(next.wallbox_meter_type,!strcmp(next.house_meter_type,"sdm320")?"sdm230":next.house_meter_type);
            strcpy(next.house_meter_type,"tasmota"); next.meter_format=0;
            next.zero_feed_enabled=false;
        }
    }
    if(version<11) {
        next.relay_board_enabled=next.phase_switch_enabled;
        next.relay1_mode=next.phase_switch_enabled?3:0;

    }
    /* Retire direct ADC acquisition without losing network settings or energy.
       An old ADC-selected setup requires explicit commissioning of a real meter. */
    if(!memchr(next.wallbox_meter_type,0,sizeof(next.wallbox_meter_type))) return false;
    if(next.ads1115_enabled || !strcmp(next.wallbox_meter_type,"ads1115") || !strcmp(next.house_meter_type,"ads1115")) {
        next.ads1115_enabled=false; next.control_verified=false; next.mode=MODE_OFF;
        if(!strcmp(next.wallbox_meter_type,"ads1115")) strcpy(next.wallbox_meter_type,"xemex");
        if(!strcmp(next.house_meter_type,"ads1115")) {
            strcpy(next.house_meter_type,"tasmota");next.grid_guard_enabled=false;
            next.zero_feed_enabled=false;next.battery_protect=false;
        }
    }
    /* Removed house-meter extrapolation must never stay active invisibly. */
    if(!strcmp(next.house_meter_type,"sdm230") || !strcmp(next.house_meter_type,"sdm120")) {
        strcpy(next.house_meter_type,"tasmota");next.zero_feed_enabled=false;
        next.grid_guard_enabled=false;next.battery_protect=false;next.control_verified=false;
    }
    if(next.house_xemex_coils!=3) { next.house_xemex_coils=3;if(!strcmp(next.house_meter_type,"xemex"))next.control_verified=false; }
    if(next.xemex_coils==2) { next.xemex_coils=1;next.control_verified=false; }
    /* Shelly Gen1 has no Modbus-TCP server. Existing selections move to the
       current 3-phase profile and fail safely until a compatible meter is selected. */
    if(!strcmp(next.house_meter_type,"shelly_gen1")) {
        strcpy(next.house_meter_type,"shelly_gen2");next.zero_feed_enabled=false;
        next.grid_guard_enabled=false;next.battery_protect=false;next.control_verified=false;
    }
    if(!strcmp(next.wallbox_meter_type,"shelly_gen1")) {
        strcpy(next.wallbox_meter_type,"shelly_gen2");next.control_verified=false;next.mode=MODE_OFF;
    }
    next.house_meter_password[0]=0;next.wallbox_meter_password[0]=0;
    next.wallbox_address=1;next.power_factor=1;
    next.reserved_http_meter_enabled=false;next.reserved_http_meter_host[0]=0;next.reserved_feedback_source=0;
    next.wallbox_rts_pin=next.xemex_rts_pin=next.house_rts_pin=-1;
    settings_fixed_pins(&next);
    settings_basic_mode(&next);
    if(!settings_valid(&next)) return false;
    *out=next; return true;
}
bool settings_apply_live(settings_t *runtime,const settings_t *before,const settings_t *after) {
    /* Compare saved configuration, not the active mode/current. A tariff-only
       save (including an unchanged full form) must never interrupt charging. */
    /* Compare fields explicitly: padding and unused bytes after string NULs
       must not turn an unchanged form into a restart request. */
#define SAME(field) if(before->field!=after->field) return false
#define TEXT_SAME(field) if(strcmp(before->field,after->field)) return false
    TEXT_SAME(wifi_ssid); TEXT_SAME(wifi_password); TEXT_SAME(mqtt_uri);
    TEXT_SAME(mqtt_username); TEXT_SAME(mqtt_password); TEXT_SAME(mqtt_prefix);
    TEXT_SAME(house_meter_type); TEXT_SAME(house_meter_host);
    TEXT_SAME(wallbox_meter_type); TEXT_SAME(house_power_path);
    TEXT_SAME(huawei_host); SAME(huawei_enabled); SAME(huawei_battery); SAME(huawei_pv); SAME(huawei_unit_id);
    SAME(version); SAME(wallbox_tx_pin); SAME(wallbox_rx_pin); SAME(wallbox_rts_pin);
    SAME(xemex_tx_pin); SAME(xemex_rx_pin); SAME(xemex_rts_pin);
    SAME(wallbox_address); SAME(xemex_address); SAME(grid_limit_a);
    SAME(max_charge_a); SAME(min_charge_a); SAME(nominal_v); SAME(power_factor);
    SAME(zero_feed_enabled); SAME(zero_reserve_w);
    SAME(xemex_coils); SAME(meter_baud); SAME(meter_format); SAME(mqtt_state_json);
    SAME(current_offset_a);
    SAME(phase_switch_enabled); SAME(phase_feedback_enabled); SAME(fixed_charge_phases); SAME(shell_limits_auto); TEXT_SAME(shell_setup_host);
    SAME(phase_feedback_closed_is_single);
    SAME(external_mode_input_enabled); SAME(evu_input_enabled);
    SAME(external_mode_input_pin); SAME(evu_input_pin); SAME(evu_limit_a);
    SAME(grid_guard_enabled); SAME(house_tx_pin); SAME(house_rx_pin); SAME(house_rts_pin);
    SAME(house_address); SAME(house_meter_baud); SAME(house_meter_format); SAME(house_xemex_coils);
    SAME(relay_board_enabled); SAME(relay_active_low); SAME(mqtt_enabled);
    SAME(relay1_pin); SAME(relay2_pin); SAME(relay1_mode); SAME(relay2_mode);
    if(before->mqtt_input_source==2||after->mqtt_input_source==2){
        SAME(mqtt_input_source);TEXT_SAME(opendtu_prefix);TEXT_SAME(opendtu_pv_topic);TEXT_SAME(opendtu_pv_valid_topic);
        SAME(opendtu_current_positive_discharge);SAME(battery_protect);SAME(pv_display_enabled);
    }


#undef SAME
#undef TEXT_SAME
    if(!range(after->price_kwh,0,10)||!range(after->solar_price_kwh,0,10)) return false;
    runtime->price_kwh=after->price_kwh;
    runtime->solar_price_kwh=after->solar_price_kwh;
    runtime->battery_protect=after->battery_protect;
    runtime->battery_reserve_soc=after->battery_reserve_soc;
    runtime->battery_cloud_limit_w=after->battery_cloud_limit_w;
    runtime->battery_assist_limit_w=after->battery_assist_limit_w;
    runtime->pv_allocation_enabled=after->pv_allocation_enabled; runtime->pv_priority=after->pv_priority;
    runtime->pv_house_priority_w=after->pv_house_priority_w; runtime->pv_car_priority_w=after->pv_car_priority_w;
    runtime->expert_mode=after->expert_mode;
    runtime->basic_mode=after->basic_mode;
    settings_basic_mode(runtime);
    runtime->control_status_visible=after->control_status_visible;
    runtime->vehicle_soc_enabled=after->vehicle_soc_enabled;
    runtime->pv_display_enabled=after->pv_display_enabled;
    runtime->homeassistant_enabled=after->homeassistant_enabled;
    runtime->mqtt_input_source=after->mqtt_input_source;
    runtime->charge_plan_enabled=after->charge_plan_enabled;
    strcpy(runtime->wallbox_meter_host,after->wallbox_meter_host);
    return true;
}
int relay_output_level(bool active_low,bool energized) { return active_low?!energized:energized; }

bool phase_fault_reset_allowed(bool faulted,bool gpio_ready,bool feedback_required,
                               unsigned observed,bool relay_on,bool measurement_fresh,
                               const float actual[3]) {
    if(!faulted || !gpio_ready || relay_on || !measurement_fresh ||
       !phase_feedback_matches(feedback_required,observed,3)) return false;
    for(unsigned p=0;p<3;p++)
        if(!isfinite(actual[p]) || actual[p]<0 || actual[p]>=1) return false;
    return true;
}

bool current_calibration_step(current_calibration_t *c,settings_t *s,bool permitted,
    float target,const float actual[3],int64_t sample_at,int64_t now) {
    /* Learn slowly from an unchanged mid-range setpoint. Never search for the
       minimum or move the user's request. No learning from PV, starts, stops,
       stale data, limiting inputs, or phase transitions. */
    float low=INFINITY,high=-INFINITY,sum=0;
    for(unsigned p=0;p<3;p++) { if(!range(actual[p],1,63))permitted=false;low=fminf(low,actual[p]);high=fmaxf(high,actual[p]);sum+=actual[p]; }
    float mean=sum/3;
    permitted=permitted && s->enabled && s->mode==MODE_MANUAL && s->charge_phases==3 &&
        fresh(now,sample_at,EMS_METER_TTL) &&
        range(target,s->min_charge_a+1.5f,s->max_charge_a-.5f) &&
        target-s->current_offset_a>s->min_charge_a+.25f &&
        low>=s->min_charge_a && high-low<=.6f && fabsf(mean-target)<=1.5f;
    if(!permitted) { c->since=0;c->samples=0;c->sum=0;return false; }
    if(!c->since || fabsf(c->target-target)>.02f || now<c->since || now-c->last_sample>EMS_METER_TTL) {
        c->since=now;c->target=target;c->samples=0;c->sum=0;c->lowest=mean;c->highest=mean;
    }
    if(sample_at<=c->last_sample)return false;
    c->last_sample=sample_at;c->lowest=fminf(c->lowest,mean);c->highest=fmaxf(c->highest,mean);
    if(c->highest-c->lowest>.4f) { c->since=0;c->samples=0;c->sum=0;return false; }
    c->sum+=mean;c->samples++;
    if(now-c->since<120000 || c->samples<60)return false;
    float error=(float)(c->sum/c->samples)-target;
    c->since=0;c->samples=0;c->sum=0;
    if(fabsf(error)<=.15f)return false;
    float offset=fminf(2,fmaxf(0,s->current_offset_a+fminf(.05f,fmaxf(-.05f,error*.1f))));
    offset=fminf(offset,fmaxf(0,s->min_charge_a-EMS_MIN_CHARGE_A));
    if(fabsf(offset-s->current_offset_a)<.001f)return false;
    s->current_offset_a=offset;c->adjustments++;return true;
}

bool reconstruct_currents(float values[3],unsigned coils) {
    if(coils<1 || coils>3) return false;
    float sum=0;
    for(unsigned i=0;i<coils;i++) { if(!range(values[i],0,999)) return false; sum+=values[i]; }
    for(unsigned i=coils;i<3;i++) values[i]=sum/coils;
    return true;
}
float estimated_charge_power(const settings_t *s,const float currents[3]) {
    float sum=0,peak=0;
    for(int p=0;p<s->charge_phases;p++) {
        if(!range(currents[p],0,999)) return NAN;
        sum+=currents[p]; peak=fmaxf(peak,currents[p]);
    }
    /* Match the dashboard idle threshold. Do not integrate the Xemex idle
       current floor as fictitious car charging; raw current stays available. */
    return peak<1?0:sum*s->nominal_v*s->power_factor;
}
float solar_current(const settings_t *s,const float actual[3],float grid_w) {
    if(!isfinite(grid_w)) return 0;
    for(int p=0;p<s->charge_phases;p++) if(!range(actual[p],0,999)) return 0;
    float sum=0; for(int p=0;p<s->charge_phases;p++) sum+=actual[p];
    float current=sum/s->charge_phases-(grid_w-s->zero_reserve_w)/(s->charge_phases*s->nominal_v);
    return fmaxf(0,fminf(s->max_charge_a,current));
}
/* Reconstruct the PV budget after household consumption from independent
   meters. Battery discharge is never counted as new solar generation. Only
   the EV load is controlled; this is not an inverter charge-power command. */
float pv_allocation_grid(const settings_t *s,const float actual[3],float grid_w,
                        float charge_w,float discharge_w,float soc,bool battery_fresh) {
    if(!s->pv_allocation_enabled || !s->battery_protect || !s->zero_feed_enabled ||
       !battery_fresh || !range(charge_w,0,30000) || !range(discharge_w,0,12000) ||
       !range(soc,0,100) || !isfinite(grid_w)) return grid_w;
    float ev=estimated_charge_power(s,actual);
    if(!isfinite(ev)) return grid_w;
    float existing=ev-grid_w-discharge_w+s->zero_reserve_w;
    float budget=fmaxf(0,existing+charge_w), car=budget;
    if(s->pv_priority==0) car=fmaxf(0,budget-(soc>=99?0:s->pv_house_priority_w));
    else if(s->pv_priority==1) car=fminf(budget,s->pv_car_priority_w);
    else if(s->pv_priority==2 && soc<99)
        car=budget*s->pv_car_priority_w/(s->pv_house_priority_w+s->pv_car_priority_w);
    /* Feed the allocation into the established ramp/battery allowance logic.
       Do not clamp to the present phase limit here: phase selection needs the
       complete budget to decide whether three phases are appropriate. */
    return grid_w+existing-car;
}

float battery_solar_current(const settings_t *s,const float actual[3],float grid_w,float soc,bool battery_fresh,float discharge_w,bool discharge_fresh) {
    if(!s->battery_protect) return solar_current(s,actual,grid_w);
    if(!battery_fresh || !range(soc,0,100) || soc<=s->battery_reserve_soc) return 0;
    /* Grid power already includes inverter output. When the battery must be
       spared, remove its measured discharge from apparent PV surplus. If that
       telemetry is stale, reserve the full rated 4 kW conservatively. */
    float reserve=discharge_fresh&&range(discharge_w,0,12000)?discharge_w:EMS_BATTERY_DISCHARGE_MAX_W;
    return solar_current(s,actual,grid_w+reserve);
}
bool parse_number(const char *text, double lo, double hi, double *out) {
    if (!text || !*text) return false;
    errno=0; char *end; double value=strtod(text,&end);
    if (end==text || errno) return false;
    while (isspace((unsigned char)*end)) end++;
    if (*end || !range(value,lo,hi)) return false;
    *out=value; return true;
}
float battery_assisted_current(const settings_t *s,const float actual[3],float grid_w,float soc,bool valid,float discharge_w,bool discharge_fresh,bool allow,bool charging) {
    float protected=battery_solar_current(s,actual,grid_w,soc,valid,discharge_w,discharge_fresh);
    if(!allow || !s->battery_protect) return protected;
    if(!valid || !range(soc,0,100) || soc<=s->battery_reserve_soc) return 0;
    if(!discharge_fresh || !range(discharge_w,0,12000)) return protected;
    /* Before charging, add only the unused part of the configured battery
       allowance. Once charging, remove discharge above that allowance. */
    float adjusted_grid=charging
        ? grid_w+fmaxf(0,discharge_w-s->battery_assist_limit_w)
        : grid_w+discharge_w-s->battery_assist_limit_w;
    return solar_current(s,actual,adjusted_grid);
}
static void battery_buffer_reset(battery_buffer_t *buffer) {
    memset(buffer,0,sizeof(*buffer));
}
float battery_cloud_current(battery_buffer_t *buffer,const settings_t *s,
                            const float actual[3],float grid_w,float soc,
                            bool battery_fresh,float discharge_w,bool discharge_fresh,
                            bool enabled,bool charging,float minimum_a,int64_t now) {
    float protected=battery_solar_current(s,actual,grid_w,soc,battery_fresh,discharge_w,discharge_fresh);
    if(!buffer) return protected;
    buffer->active=false; buffer->remaining_ms=0;
    if(!enabled || !s->battery_protect || !battery_fresh || !range(soc,0,100) ||
       soc<=s->battery_reserve_soc || !charging || !range(minimum_a,EMS_MIN_CHARGE_A,63)) {
        battery_buffer_reset(buffer); return protected;
    }
    /* A full minute of PV-only operation rearms the complete cloud allowance.
       Short sunny gaps therefore do not create an unlimited repeated drain. */
    if(protected>=minimum_a) {
        if(buffer->started_at>0) {
            if(buffer->recovery_at<=0) buffer->recovery_at=now;
            if(now>=buffer->recovery_at && now-buffer->recovery_at>=EMS_BATTERY_BUFFER_RECOVERY_MS)
                battery_buffer_reset(buffer);
        }
        return protected;
    }
    buffer->recovery_at=0;
    if(buffer->started_at<=0) buffer->started_at=now;
    if(now<buffer->started_at || now-buffer->started_at>=EMS_BATTERY_BUFFER_MS) return protected;
    buffer->remaining_ms=(uint32_t)(EMS_BATTERY_BUFFER_MS-(now-buffer->started_at));
    if(!discharge_fresh || !range(discharge_w,0,12000) || discharge_w<1) return protected;
    /* Remove discharge above the configured cloud-buffer allowance from the
       apparent PV surplus before calculating current. */
    float excess=fmaxf(0,discharge_w-s->battery_cloud_limit_w);
    float supported=solar_current(s,actual,grid_w+excess);
    buffer->active=supported>=minimum_a && supported>protected+0.05f;
    return supported;
}
float pv_ramp_available_phases(float available,float target,const float actual[3],unsigned phases) {
    if(phases!=1 && phases!=3) return 0;
    if(target<=0) return available; /* start threshold uses actual surplus */
    float sum=0,peak=0;
    for(unsigned p=0;p<phases;p++) { if(!range(actual[p],0,999)) return 0; sum+=actual[p]; peak=fmaxf(peak,actual[p]); }
    /* No upward ramp before the vehicle draws current; keep the start request.
       Afterwards allow at most 1 A ahead of measured vehicle current. */
    return fminf(available,peak<1?target:sum/phases+1.0f);
}
float pv_ramp_available(float available,float target,const float actual[3]) {
    return pv_ramp_available_phases(available,target,actual,3);
}
static void pv_stop(pv_control_t *c,int64_t now) {
    if(c->target_a>0) { c->stopped=true; c->stopped_at=now; }
    c->target_a=0; c->above_at=-1; c->below_at=-1; c->wait_ms=0;
}
void pv_control_step(pv_control_t *c,bool permitted,float available,float minimum,float maximum,int64_t now) {
    bool clock_reset=c->initialized && now<c->last_at;
    bool stalled=c->initialized && now-c->last_at>5000;
    if(!c->initialized || clock_reset) {
        memset(c,0,sizeof(*c)); c->initialized=true; c->above_at=-1; c->below_at=-1;
        if(clock_reset) { c->stopped=true; c->stopped_at=now; }
    }
    c->last_at=now; c->wait_ms=0;
    if(!permitted || stalled || !range(available,0,63) || !range(minimum,6,63) || !range(maximum,minimum,63)) {
        pv_stop(c,now); c->phase=PV_BLOCKED; return;
    }
    if(c->target_a<=0) {
        if(c->stopped && now-c->stopped_at<120000) {
            c->phase=PV_COOLDOWN; c->above_at=-1;
            c->wait_ms=(uint32_t)(120000-(now-c->stopped_at)); return;
        }
        float start_threshold=fminf(maximum,minimum+1.0f);
        if(available<start_threshold) { c->above_at=-1; c->phase=PV_WAITING; return; }
        if(c->above_at<0) c->above_at=now;
        if(now-c->above_at<60000) {
            c->phase=PV_STARTING; c->wait_ms=(uint32_t)(60000-(now-c->above_at)); return;
        }
        c->target_a=start_threshold; c->adjusted_at=now; c->below_at=-1; c->phase=PV_RUNNING;
        return;
    }
    if(available<minimum-0.5f) {
        if(c->below_at<0) c->below_at=now;
        if(now-c->below_at>=30000) {
            pv_stop(c,now); c->phase=PV_COOLDOWN; c->wait_ms=120000; return;
        }
        c->phase=PV_STOPPING; c->wait_ms=(uint32_t)(30000-(now-c->below_at));
    } else { c->below_at=-1; c->phase=PV_RUNNING; }
    float desired=fmaxf(minimum,fminf(maximum,available));
    /* Increase twice as often; retain the proven downward timing. The caller
       still caps the request to 1 A ahead of measured vehicle current. */
    int64_t adjust_interval=desired>c->target_a?5000:10000;
    if(now-c->adjusted_at>=adjust_interval) {
        if(fabsf(desired-c->target_a)>=0.2f) c->target_a=fminf(desired,c->target_a+1.0f);
        c->adjusted_at=now;
    }
}
bool pv_control_takeover(pv_control_t *c,bool permitted,float previous,
                         const float actual[3],unsigned phases,float minimum,float maximum,int64_t now) {
    if(!permitted || (phases!=1 && phases!=3) || !range(previous,minimum,maximum))return false;
    float sum=0;
    for(unsigned p=0;p<phases;p++){if(!range(actual[p],0,999))return false;sum+=actual[p];}
    if(sum/phases<1)return false; /* idle/unplugged must retain the normal start delay */
    memset(c,0,sizeof(*c));c->initialized=true;c->last_at=c->adjusted_at=now;
    c->above_at=c->below_at=-1;c->phase=PV_RUNNING;
    c->target_a=fmaxf(minimum,fminf(previous,sum/phases));
    return true;
}

const char *mode_name(control_mode_t m) {
    static const char *names[]={"off","manual","grid_limit","pv"};
    return m>=MODE_OFF && m<=MODE_PV ? names[m] : "off";
}
bool parse_mode(const char *text, control_mode_t *out) {
    if (!text) return false;
    for (int i=MODE_OFF;i<=MODE_PV;i++) if (!strcmp(text,mode_name(i))) { *out=i; return true; }
    return false;
}
bool fresh(int64_t now,int64_t last,int64_t ttl) { return last>0 && now>=last && now-last<=ttl; }
bool phase_option_change_allowed(bool was_enabled,bool requested_enabled,
                                 unsigned active_phases,bool phase_ready,
                                 bool meter_fresh,const float actual[3]) {
    if(was_enabled==requested_enabled) return true;
    if(!meter_fresh || !actual) return false;
    for(unsigned p=0;p<3;p++) if(!range(actual[p],0,0.999f)) return false;
    /* An observed phase-switch fault must not prevent a stopped installation
       from switching the experimental option off. Enabling still requires the
       normal three-phase position and a healthy state machine. */
    return !requested_enabled || (active_phases==3 && phase_ready);
}
ems_wifi_mode_t wifi_recovery_mode(bool configured,bool online,bool forced_ap,int64_t now,int64_t lost_at) {
    if(!configured || forced_ap) return EMS_WIFI_AP;
    if(online) return EMS_WIFI_STA;
    if(lost_at>0 && now>=lost_at && now-lost_at>=300000) return EMS_WIFI_AP_STA;
    return EMS_WIFI_STA;
}
float control_target(const settings_t *s,bool meter_ok,bool feedback_ok,bool pv_ok) {
    if (!s->enabled || s->mode==MODE_OFF || !meter_ok || !feedback_ok) return 0;
    if (s->mode==MODE_PV && !pv_ok) return 0;
    float target=s->mode==MODE_MANUAL ? s->manual_current_a : s->mode==MODE_PV ? s->pv_surplus_a : s->max_charge_a;
    float minimum=s->min_charge_a;
    if (!isfinite(target) || target<minimum) return 0;
    return fminf(target,s->max_charge_a);
}
bool house_phase_currents_ready(bool supported,const float house[3],int64_t now,int64_t measured_at) {
    if(!supported || !house || !fresh(now,measured_at,EMS_HOUSE_TTL)) return false;
    for(int p=0;p<3;p++) if(!range(house[p],0,1000)) return false;
    return true;
}
float grid_guard_target(const settings_t *s,float requested,bool house_ready) {
    if(!isfinite(requested) || requested<=0) return 0;
    if(!s->grid_guard_enabled || house_ready) return requested;
    /* Missing house phase currents remove only the per-phase house protection.
       Keep the normal Wallbox feedback loop, with an 8 kW target ceiling.
       Never increase a smaller request or override another stop condition. */
    float watts_per_amp=s->charge_phases*s->nominal_v*s->power_factor;
    if(!isfinite(watts_per_amp) || watts_per_amp<=0) return 0;
    return fminf(requested,fminf(s->max_charge_a,EMS_GRID_FALLBACK_W/watts_per_amp));
}
float charge_plan_maximum_kw(const settings_t *s,bool house_ready,bool evu_active) {
    /* Plans request three phases for their final grid completion. Account for
       all configured ceilings when deciding when that completion must start. */
    float current=s->max_charge_a;
    if(s->evu_input_enabled && evu_active) current=fminf(current,s->evu_limit_a);
    unsigned phases=s->phase_switch_enabled?3:s->fixed_charge_phases;
    float watts=current*phases*s->nominal_v*s->power_factor;
    if(s->grid_guard_enabled && !house_ready) watts=fminf(watts,EMS_GRID_FALLBACK_W);
    return isfinite(watts)?fmaxf(0,watts/1000):0;
}
bool phase_feedback_matches(bool required,unsigned observed,unsigned expected) {
    return (expected==1 || expected==3) && (!required || observed==expected);
}
unsigned phase_feedback_position(bool contact_closed,bool closed_is_single) {
    return contact_closed==closed_is_single?1:3;
}
bool phase_motion_complete(bool required,unsigned observed,unsigned expected,int64_t elapsed_ms) {
    return phase_feedback_matches(required,observed,expected) && elapsed_ms>=0 && (required || elapsed_ms>=2000);
}
void grid_guard_report(const settings_t *s,bool house_ready,const float house[3],float reported[3]) {
    /* The single-phase Shell input only has L1. Inactive charging phases must
       stay zero even when a three-phase house meter is available. */
    unsigned phases=s->charge_phases==1?1:3;
    for(unsigned p=phases;p<3;p++)reported[p]=0;
    if(!s->grid_guard_enabled || !house_ready) return;
    for(unsigned p=0;p<phases;p++) reported[p]=fmaxf(reported[p],house[p]);
}
bool charge_guard_blocked(const charge_guard_t *g,int64_t now) {
    return g->latched || now<g->retry_until;
}
void charge_guard_step(charge_guard_t *g,bool requested,bool feedback_ok,const float actual[3],float minimum_a,int64_t now) {
    if(g->latched) return;
    if(now<g->last_at) { memset(g,0,sizeof(*g)); g->retry_until=now+30000; }
    if(now-g->last_at>EMS_METER_TTL) g->low_since=g->charging_since=0;
    g->last_at=now;
    unsigned keep=0;
    for(unsigned i=0;i<g->stop_count;i++) if(now-g->stops[i]<EMS_CHARGE_STOP_WINDOW_MS) g->stops[keep++]=g->stops[i];
    g->stop_count=keep;
    if(!requested) { g->seen_charging=false; g->low_since=g->charging_since=0; return; }
    if(now<g->retry_until) return;
    if(!feedback_ok) { g->low_since=g->charging_since=0; return; }
    float peak=0;
    for(int p=0;p<3;p++) { if(!range(actual[p],0,999)) { g->low_since=0; return; } peak=fmaxf(peak,actual[p]); }
    /* A brief phantom/starting current must not count as a completed charge.
       Require sustained current near the commissioned minimum first. */
    if(peak>=fmaxf(4,minimum_a-1.5f)) {
        if(!g->charging_since)g->charging_since=now;
        if(now-g->charging_since>=8000)g->seen_charging=true;
    }else g->charging_since=0;
    if(peak>=1 || !g->seen_charging) { g->low_since=0; return; }
    /* Idle without a car never arms the guard. The Shell may briefly pause
       while applying a new DLB value, so only treat a continuous 20 s loss
       of current as a physical stop. Retry after 30 s and latch only after
       five confirmed stops in a rolling 5 min. A 3 min window could never
       contain five stops with the required confirmation and retry times. */
    if(!g->low_since) g->low_since=now;
    if(now-g->low_since<EMS_CHARGE_STOP_CONFIRM_MS) return;
    g->seen_charging=false; g->low_since=g->charging_since=0;
    g->stops[g->stop_count++]=now;
    g->latched=g->stop_count>=EMS_CHARGE_STOP_LIMIT; g->retry_until=now+30000;
}
void control_report(const settings_t *s,const float actual[3],float target,float house_power_w,bool house_power_ok,float out[3]) {
    for (int p=0;p<3;p++) {
        if(s->charge_phases==1 && p>0){out[p]=0;continue;}
        /* Shell DLB asks for grid-connection current. Manual/grid-limit control
           must include the measured Wallbox current in the feedback equation;
           feeding the house meter into that path makes an 8 A target unstable. */
        bool feedback=range(actual[p],0,999);
        float minimum=s->min_charge_a;
        float desired=range(target,minimum,s->max_charge_a)?target:0;
        /* The target is the measured vehicle current. The Shell/Xemex path
           settles above its DLB request, so compensate that measured offset.
           The protocol floor remains 6 A; the commissioned 8 A floor applies
           to the measured target and must not suppress this compensation. */
        if(desired>0) desired=fmaxf(EMS_MIN_CHARGE_A,desired-s->current_offset_a);
        /* A stop must remove the entire configured charging headroom, including
           while the car is still drawing current. A 1 A overload is only a
           small reduction request, not an unambiguous zero-headroom request.
           Keep this asserted at zero measured current to prevent restarting.
           Delivery and physical stop still depend on the Shell DLB link. */
        float requested_grid=s->grid_limit_a+s->max_charge_a+1.0f;
        if(desired>0 && feedback) {
            float error=desired-actual[s->charge_phases==1?0:p];
            /* Correct the full current error in both directions. Halving the
               downward error left a persistent measured offset near 8 A. */
            requested_grid=s->grid_limit_a-error;
            if(fabsf(error)<0.25f) requested_grid=s->grid_limit_a;
        }
        float outside=NAN;
        if(s->mode==MODE_PV && house_power_ok && isfinite(house_power_w))
            outside=house_power_w/(s->charge_phases*s->nominal_v*s->power_factor)-(feedback?actual[s->charge_phases==1?0:p]:0);
        /* Without a fresh house value, retain a deliberately conservative fallback.
           It never pretends that wallbox current is external household load. */
        if(!isfinite(outside)) outside=requested_grid;
        out[p]=fmaxf(0,fmaxf(requested_grid,outside));
    }
}
uint16_t modbus_crc(const uint8_t *d,size_t n) {
    uint16_t crc=0xffff;
    for(size_t i=0;i<n;i++) { crc^=d[i]; for(int j=0;j<8;j++) crc=(crc&1)?(crc>>1)^0xa001:crc>>1; }
    return crc;
}
void append_crc(uint8_t *d,size_t n) { uint16_t c=modbus_crc(d,n); d[n]=c; d[n+1]=c>>8; }
bool valid_frame(const uint8_t *d,size_t n) { return n>=4 && modbus_crc(d,n)==0; }
static float get_float(const uint8_t *d) {
    uint32_t raw=((uint32_t)d[0]<<24)|((uint32_t)d[1]<<16)|((uint32_t)d[2]<<8)|d[3];
    float f; memcpy(&f,&raw,4); return f;
}
bool decode_meter(const uint8_t *d,size_t n,uint8_t address,float out[3]) {
    if(n!=17 || !valid_frame(d,n) || d[0]!=address || d[1]!=3 || d[2]!=12) return false;
    float values[3];
    for(int p=0;p<3;p++) { values[p]=get_float(d+3+p*4); if(!range(values[p],0,999)) return false; }
    memcpy(out,values,sizeof(values)); return true;
}
bool decode_sdm(const uint8_t *d,size_t n,uint8_t address,float *out,unsigned count,float lo,float hi) {
    if(!count || count>3 || n!=5+count*4 || !valid_frame(d,n) || d[0]!=address || d[1]!=4 || d[2]!=count*4) return false;
    float values[3];
    for(unsigned p=0;p<count;p++) { values[p]=get_float(d+3+p*4); if(!range(values[p],lo,hi)) return false; }
    memcpy(out,values,count*sizeof(float)); return true;
}
const sdm_profile_t *sdm_profile(const char *type) {
    static const sdm_profile_t sdm230={0x0006,0x000c,1},sdm630={0x0006,0x0034,3};
    if(!strcmp(type,"sdm230") || !strcmp(type,"sdm120")) return &sdm230;
    if(!strcmp(type,"sdm630") || !strcmp(type,"sdm630mct")) return &sdm630;
    return NULL;
}
bool decode_sdm_reading(const sdm_profile_t *profile,uint8_t address,
    const uint8_t *currents,size_t current_length,const uint8_t *power,size_t power_length,
    float out[3],float *total_w) {
    if(!profile || (profile->phases!=1 && profile->phases!=3)) return false;
    float measured[3]={0},watts;
    if(!decode_sdm(currents,current_length,address,measured,profile->phases,0,999) ||
       !decode_sdm(power,power_length,address,&watts,1,0,333333)) return false;
    /* SDM230 measures one phase of this user's balanced three-phase Wallbox.
       Only the phase current is copied; the total active power is multiplied. */
    if(profile->phases==1) { measured[1]=measured[2]=measured[0]; watts*=3; }
    memcpy(out,measured,sizeof(measured)); *total_w=watts; return true;
}
static bool reg_value(uint16_t reg,const settings_t *s,const float currents[3],uint16_t *out) {
    if(reg>=0x4000 && reg<=0x400f) {
        const uint16_t info[]={0,1,20802,s->wallbox_address,9600,0x3f80,0,0x4000,0,0x3f80,0,80,2000,0x24,1,0};
        *out=info[reg-0x4000]; return true;
    }
    if(reg<0x500c || reg>0x5011) return false;
    uint32_t raw; memcpy(&raw,&currents[(reg-0x500c)/2],4);
    *out=(reg&1)?raw&0xffff:raw>>16; return true;
}
size_t modbus_reply(const uint8_t *req,size_t n,uint8_t *out,const settings_t *s,const float currents[3]) {
    if(n!=8 || !valid_frame(req,n) || req[0]!=s->wallbox_address) return 0;
    uint8_t error=0; uint16_t start=(req[2]<<8)|req[3],count=(req[4]<<8)|req[5];
    if(req[1]!=3 && req[1]!=4) error=1;
    else if(!count || count>125) error=3;
    else if((uint32_t)start+count>0x10000) error=2;
    out[0]=req[0]; out[1]=req[1]; out[2]=(uint8_t)(count*2);
    if(!error) for(uint16_t i=0;i<count;i++) {
        uint16_t value;
        if(!reg_value(start+i,s,currents,&value)) { error=2; break; }
        out[3+i*2]=value>>8; out[4+i*2]=value;
    }
    size_t len=error?3:3+count*2;
    if(error) { out[1]|=0x80; out[2]=error; }
    append_crc(out,len); return len+2;
}
static energy_day_t *day_bucket(energy_store_t *s,uint32_t date) {
    int oldest=0;
    for(int i=0;i<EMS_DAYS;i++) {
        if(s->days[i].date==date) return &s->days[i];
        if(s->days[i].date<s->days[oldest].date) oldest=i;
    }
    if(s->days[oldest].date>date) return NULL;
    memset(&s->days[oldest],0,sizeof(s->days[oldest])); s->days[oldest].date=date;
    return &s->days[oldest];
}
static void add_day(energy_store_t *s,uint32_t date,int source,double wh,int64_t ms,bool charging) {
    energy_day_t *day=date?day_bucket(s,date):NULL;
    if(day) { day->wh[source]+=wh; day->covered_ms[source]+=ms; if(source==0 && charging) day->charging_ms+=ms; }
    else s->undated_wh[source]+=wh;
}
void energy_step(energy_t *e,int64_t now,int64_t epoch,uint32_t date,int64_t midnight,
                 const double power[2],const int64_t expires[2]) {
    if(e->started && now>e->last_ms && now-e->last_ms<=5000) {
        int64_t dt=now-e->last_ms;
        bool clock_ok=date && e->last_date && llabs((epoch-e->last_epoch_ms)-dt)<=2000;
        for(int s=0;s<2;s++) {
            int64_t end=now<e->expires[s]?now:e->expires[s];
            int64_t ms=end-e->last_ms;
            if(ms<=0 || !range(e->previous_w[s],0,1000000)) continue;
            double wh=e->previous_w[s]*ms/3600000.0;
            e->store.total_wh[s]+=wh; e->boot_wh[s]+=wh;
            if(clock_ok && date!=e->last_date) {
                int64_t before=midnight-e->last_epoch_ms;
                if(before<0) before=0;
                if(before>ms) before=ms;
                add_day(&e->store,e->last_date,s,e->previous_w[s]*before/3600000.0,before,e->previous_w[s]>=1000);
                add_day(&e->store,date,s,e->previous_w[s]*(ms-before)/3600000.0,ms-before,e->previous_w[s]>=1000);
            } else add_day(&e->store,clock_ok?date:0,s,wh,ms,e->previous_w[s]>=1000);
            /* Split intervals at minute boundaries for a real average, including gaps. */
            for(int64_t t=e->last_ms;t<end;) {
                int64_t minute=t/60000, until=(minute+1)*60000;
                if(until>end) until=end;
                energy_point_t *point=&e->history[minute%EMS_HISTORY];
                if(point->minute!=minute) { memset(point,0,sizeof(*point)); point->minute=minute; }
                point->wh[s]+=e->previous_w[s]*(until-t)/3600000.0;
                point->covered_ms[s]+=(uint32_t)(until-t); t=until;
            }
        }
    }
    e->store.version=2; e->started=true; e->last_ms=now; e->last_epoch_ms=epoch; e->last_date=date;
    for(int s=0;s<2;s++) {
        e->previous_w[s]=power[s];
        e->expires[s]=range(power[s],0,1000000)?expires[s]:0;
    }
}
bool energy_store_valid(const energy_store_t *s) {
    if(s->version!=2) return false;
    for(int p=0;p<2;p++) {
        if(!range(s->total_wh[p],0,1e15) || !range(s->undated_wh[p],0,s->total_wh[p]+0.01)) return false;
        for(int d=0;d<EMS_DAYS;d++)
            if(!range(s->days[d].wh[p],0,s->total_wh[p]+0.01) || s->days[d].covered_ms[p]>172800000) return false;
    }
    for(int d=0;d<EMS_DAYS;d++) if(s->days[d].charging_ms>s->days[d].covered_ms[0]) return false;
    return true;
}
void manual_report_smooth(manual_report_filter_t *filter,const settings_t *s,float target,int64_t now,float report[3]) {
    if(s->charge_phases==1) {
        report[1]=report[2]=0;
        filter->reported[1]=filter->reported[2]=0;
    }
    if(s->mode!=MODE_MANUAL||target<=0||!isfinite(target)) { filter->active=false; return; }
    if(s->charge_phases==1) {
        /* A lagging synthetic overload keeps lowering the Shell limit even
           after measured L1 has fallen. Release an obsolete reduction at once;
           only building a new reduction is ramped. Start from neutral load so
           idle feedback cannot add a long climb from grid-limit minus target. */
        if(!isfinite(report[0]) || report[0]>=s->grid_limit_a+s->max_charge_a+1.0f) {
            filter->active=false;return;
        }
        if(!filter->active||now<=filter->last_ms||now-filter->last_ms>5000) {
            filter->reported[0]=fmaxf(s->grid_limit_a,report[0]);
            filter->last_ms=now;filter->active=true;report[0]=filter->reported[0];return;
        }
        float delta=report[0]-filter->reported[0];
        if(delta<0 && filter->reported[0]>s->grid_limit_a) {
            filter->reported[0]=fmaxf(s->grid_limit_a,report[0]);
        } else {
            float error=report[0]-s->grid_limit_a;
            float rate=delta<0?0.3f:error>2.0f?0.5f:0.15f;
            float max_delta=rate*(now-filter->last_ms)/1000.0f;
            filter->reported[0]+=fminf(max_delta,fmaxf(-max_delta,delta));
        }
        report[0]=filter->reported[0];filter->last_ms=now;return;
    }
    if(!filter->active||now<=filter->last_ms||now-filter->last_ms>5000) {
        memcpy(filter->reported,report,sizeof(filter->reported));filter->last_ms=now;filter->active=true;return;
    }
    /* Near the proven 6 kW three-phase floor, approach the final limit particularly
       gently. The car first starts with useful headroom; TeeNet then closes
       the remaining error at 0.05 A/s. Mid-range changes stay responsive and
       the upper range retains its calmer proven ramp. */
    float rate=target<=8.8f?0.05f:target<9?0.15f:target<13?0.4f:0.2f;
    for(int p=0;p<3;p++) {
        float delta=report[p]-filter->reported[p];
        /* Lower simulated grid load grants MORE charging current. Only that
           direction is accelerated; reductions keep the measured slow ramp. */
        float max_delta=rate*(delta<0?2.0f:1.0f)*(now-filter->last_ms)/1000.0f;
        filter->reported[p]+=fminf(max_delta,fmaxf(-max_delta,delta));
        report[p]=filter->reported[p];
    }
    filter->last_ms=now;
}
typedef struct {
    uint32_t date;
    double wh[EMS_SOURCES];
    uint64_t covered_ms[EMS_SOURCES];
} energy_day_v1_t;
typedef struct {
    uint32_t version;
    double total_wh[EMS_SOURCES],undated_wh[EMS_SOURCES];
    energy_day_v1_t days[EMS_DAYS];
} energy_store_v1_t;
bool energy_store_decode(const void *blob,size_t length,energy_store_t *out) {
    if(!blob || !out) return false;
    energy_store_t decoded={0};
    if(length==sizeof(decoded)) {
        memcpy(&decoded,blob,sizeof(decoded));
    } else if(length==sizeof(energy_store_v1_t)) {
        const energy_store_v1_t *old=blob;
        if(old->version!=1) return false;
        decoded.version=2;
        memcpy(decoded.total_wh,old->total_wh,sizeof(decoded.total_wh));
        memcpy(decoded.undated_wh,old->undated_wh,sizeof(decoded.undated_wh));
        for(int d=0;d<EMS_DAYS;d++) {
            decoded.days[d].date=old->days[d].date;
            memcpy(decoded.days[d].wh,old->days[d].wh,sizeof(decoded.days[d].wh));
            memcpy(decoded.days[d].covered_ms,old->days[d].covered_ms,sizeof(decoded.days[d].covered_ms));
        }
    } else return false;
    if(!energy_store_valid(&decoded)) return false;
    *out=decoded; return true;
}
