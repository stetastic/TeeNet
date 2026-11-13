#include "ems_core.h"
#include "hardware_profile.h"
#include <assert.h>
#include <stdio.h>
#include <string.h>

static void switching(settings_t *s,unsigned actuator){
    settings_defaults(s);s->basic_mode=false;s->phase_switch_enabled=s->relay_board_enabled=true;
    s->relay1_mode=3;s->phase_actuator_type=actuator;s->phase_feedback_source=2;
    strcpy(s->wallbox_meter_type,"shelly_gen2");strcpy(s->wallbox_meter_host,"192.0.2.10");
    strcpy(s->phase_shelly_host,"192.0.2.11");s->phase_shelly_model=4;
    if(actuator==2){s->mqtt_enabled=s->phase_mqtt_output_enabled=true;strcpy(s->phase_mqtt_topic,"wallbox-ems/output/single_phase");}
}
int main(void){
    settings_t s,decoded;settings_defaults(&s);
    assert(settings_valid(&s)&&EMS_GPIO_ROLES==10);
    for(unsigned i=0;i<EMS_GPIO_ROLES;i++)assert(strncmp(gpio_roles[i].id,"phase_feedback",14));
    for(unsigned actuator=0;actuator<4;actuator++){
        switching(&s,actuator);s.phase_no_feedback_acknowledged=false;
        assert(settings_valid(&s)); /* No contact, warning acknowledgement or expert grant required. */
        s.phase_feedback_source=1;s.phase_feedback_enabled=true;
        assert(!settings_valid(&s)); /* Opt-in voltage proof needs its meter address. */
        strcpy(s.phase_voltage_host,"192.0.2.10");assert(settings_valid(&s));
        s.phase_shelly_pulse=true;assert(!settings_valid(&s));
    }
    switching(&s,1);s.version=36;s.phase_feedback_source=0;s.phase_feedback_enabled=true;
    s.phase_feedback_pin=13;s.phase_feedback_second_pin=14;strcpy(s.wifi_ssid,"preserved-network");
    strcpy(s.mqtt_password,"synthetic-secret");
    size_t legacy_size=(offsetof(settings_t,phase_contactor_reconfigure)+3)&~(size_t)3;
    assert(settings_decode(&s,legacy_size,&decoded));
    assert(decoded.phase_switch_enabled&&decoded.phase_feedback_source==2&&!decoded.phase_feedback_enabled);
    assert(decoded.phase_feedback_pin==-1&&decoded.phase_feedback_second_pin==-1);
    assert(!strcmp(decoded.wifi_ssid,s.wifi_ssid)&&!strcmp(decoded.mqtt_password,s.mqtt_password));
    s.phase_shelly_pulse=true;assert(settings_decode(&s,legacy_size,&decoded));
    assert(!decoded.phase_switch_enabled&&!decoded.phase_shelly_pulse&&decoded.phase_contactor_reconfigure);
    switching(&s,0);s.charge_phases=1;float amps[3]={8,0,0};
    assert(!phase_current_mismatch(&s,amps));amps[1]=8;assert(phase_current_mismatch(&s,amps));
    amps[1]=0;amps[2]=8;assert(phase_current_mismatch(&s,amps));
    s.charge_phases=3;assert(!phase_current_mismatch(&s,amps)); /* Single-phase cars remain valid on a 3-phase supply. */
#if defined(CONFIG_TEE_BOARD_ATOMS3_LITE) && CONFIG_TEE_BOARD_ATOMS3_LITE
    settings_defaults(&s);s.basic_mode=false;s.external_mode_input_enabled=s.evu_input_enabled=true;
    assert(s.wallbox_tx_pin==2&&s.wallbox_rx_pin==1&&s.external_mode_input_pin==5&&s.evu_input_pin==6);
    assert(s.phase_actuator_type==1&&settings_valid(&s));
    s.phase_actuator_type=0;s.relay_board_enabled=s.phase_switch_enabled=true;s.relay1_mode=3;
    assert(s.relay1_pin==7&&settings_valid(&s));
    s.evu_input_pin=2;assert(!settings_valid(&s));
    const int available[]={1,2,5,6,7,8,38,39};
    for(int pin=0;pin<49;pin++){
        bool exposed=false;for(unsigned i=0;i<sizeof(available)/sizeof(*available);i++)exposed|=pin==available[i];
        assert((!settings_gpio_reason(pin,false)[0])==exposed);
    }
    settings_defaults(&s);s.version=36;s.external_mode_input_pin=1;s.evu_input_pin=7;
    assert(settings_decode(&s,legacy_size,&decoded)&&decoded.external_mode_input_pin==5&&decoded.evu_input_pin==6);
    s.shell_rs485_interface=0;s.wallbox_tx_pin=6;s.wallbox_rx_pin=5;
    s.external_mode_input_pin=8;s.external_mode_input_enabled=true;
    assert(settings_decode(&s,legacy_size,&decoded)&&decoded.wallbox_tx_pin==6&&decoded.wallbox_rx_pin==5&&decoded.external_mode_input_pin==8);
#endif
    puts("PASS: optional voltage, no contact requirements, Hager absolute output, safe legacy migration, current contradiction and board GPIO budgets");
}
