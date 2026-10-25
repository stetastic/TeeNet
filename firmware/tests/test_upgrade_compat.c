#include "ems_core.h"
#include <assert.h>
#include <stdio.h>
#include <string.h>

static size_t schema_size(unsigned v){
    size_t n=0;
    switch(v){
        case 23:n=offsetof(settings_t,control_status_visible);break;
        case 24:n=offsetof(settings_t,basic_mode);break;
        case 25:n=offsetof(settings_t,opendtu_prefix);break;
        case 26:n=offsetof(settings_t,shell_rs485_interface);break;
        case 27:n=offsetof(settings_t,phase_actuator_type);break;
        case 28:n=offsetof(settings_t,phase_mqtt_output_enabled);break;
        case 29:n=offsetof(settings_t,phase_mqtt_true_three_phase);break;
        case 30:n=offsetof(settings_t,phase_output_transport);break;
        case 31:n=offsetof(settings_t,phase_feedback_count);break;
        case 32:n=offsetof(settings_t,phase_feedback_pin);break;
        case 33:n=offsetof(settings_t,heatpump_enabled);break;
        case 34:n=sizeof(settings_t);break;
        default:assert(0);
    }
    return (n+3)&~(size_t)3;
}
int main(void){
    unsigned cases=0;
    for(unsigned version=23;version<=EMS_SETTINGS_VERSION;version++)
    for(unsigned low=0;low<2;low++)for(unsigned closed_single=0;closed_single<2;closed_single++){
        settings_t stored,decoded;settings_defaults(&stored);
        stored.version=version;stored.basic_mode=false;
        stored.relay_board_enabled=true;stored.phase_switch_enabled=true;stored.relay1_mode=3;
        stored.relay_active_low=low;stored.phase_feedback_closed_is_single=closed_single;
        strcpy(stored.wifi_ssid,"Existing network");strcpy(stored.wifi_password,"Existing secret");
        strcpy(stored.mqtt_uri,"mqtt://192.0.2.10:1883");
        assert(settings_decode(&stored,schema_size(version),&decoded));
        if(decoded.relay_active_low!=(bool)low){fprintf(stderr,"schema %u changed relay polarity\n",version);assert(0);}
        if(decoded.phase_feedback_closed_is_single!=(bool)closed_single){fprintf(stderr,"schema %u inverted feedback assignment\n",version);assert(0);}
        assert(!strcmp(decoded.wifi_ssid,stored.wifi_ssid)&&!strcmp(decoded.wifi_password,stored.wifi_password));
        assert(!strcmp(decoded.mqtt_uri,stored.mqtt_uri));
        assert(decoded.phase_switch_enabled&&decoded.phase_feedback_enabled&&!decoded.enabled);
        assert(decoded.relay1_pin==12&&decoded.phase_feedback_pin==13);
        cases++;
    }
    printf("PASS: %u stored GPIO polarity/contact assignments survive upgrade; credentials retained, charging stopped\n",cases);
}
