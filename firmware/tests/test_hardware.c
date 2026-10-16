#include "ems_core.h"
#include <math.h>
#include <assert.h>
#include <stdio.h>
#include <string.h>

int main(void) {
    settings_t s,out;settings_defaults(&s);assert(settings_valid(&s));
    /* Shell UART pins persist through save/reboot; all other wiring stays fixed. */
    assert(s.wallbox_tx_pin==17 && s.wallbox_rx_pin==18);
    s.wallbox_tx_pin=8;s.wallbox_rx_pin=9;s.control_verified=true;
    settings_fixed_pins(&s);assert(s.wallbox_tx_pin==8&&s.wallbox_rx_pin==9&&s.control_verified);
    assert(settings_valid(&s)&&settings_decode(&s,sizeof(s),&out));
    assert(out.wallbox_tx_pin==8&&out.wallbox_rx_pin==9);
    assert(settings_shell_pin_available(&s,1,true));assert(settings_shell_pin_available(&s,2,false));
    const int reserved[]={0,3,19,20,22,26,32,33,34,35,36,37,45,46,48,49,-1};
    for(unsigned i=0;i<sizeof(reserved)/sizeof(reserved[0]);i++){
        assert(!settings_shell_pin_available(&s,reserved[i],true));
        s.wallbox_tx_pin=reserved[i];assert(!settings_valid(&s));
    }
    s.wallbox_tx_pin=8;
    assert(!settings_shell_pin_available(&s,9,true));
    assert(!settings_shell_pin_available(&s,4,true)&&!settings_shell_pin_available(&s,5,false));
    s.external_mode_input_enabled=true;assert(!settings_shell_pin_available(&s,6,true));
    s.evu_input_enabled=true;assert(!settings_shell_pin_available(&s,7,true));
    s.relay_board_enabled=true;assert(!settings_shell_pin_available(&s,12,true)&&settings_shell_pin_available(&s,14,false));
    s.phase_switch_enabled=true;s.relay1_mode=3;assert(!settings_shell_pin_available(&s,13,true));
    s.phase_feedback_enabled=false;assert(settings_shell_pin_available(&s,13,true));
    s.phase_feedback_enabled=true;
    strcpy(s.house_meter_type,"sdm630");assert(!settings_shell_pin_available(&s,43,true)&&!settings_shell_pin_available(&s,44,false));
    s.wallbox_tx_pin=43;assert(!settings_valid(&s));
    settings_defaults(&s);
    s.wallbox_tx_pin=43;s.version=10;
    assert(settings_decode(&s,(offsetof(settings_t,mqtt_input_source)+3)&~(size_t)3,&out));
    assert(out.wallbox_rts_pin==-1 && !out.relay_board_enabled && !out.ads1115_enabled);
    settings_defaults(&s);s.wallbox_rts_pin=10;s.xemex_rts_pin=11;
    s.relay_board_enabled=true;s.relay_active_low=true;assert(settings_valid(&s));
    s.relay2_pin=11;assert(settings_valid(&s));s.relay2_pin=14;
    s.phase_switch_enabled=true;assert(!settings_valid(&s));s.relay1_mode=3;assert(settings_valid(&s));
    s.relay1_pin=13;assert(!settings_valid(&s));s.relay1_pin=12;
    s.evu_input_enabled=true;s.evu_input_pin=12;assert(!settings_valid(&s));s.evu_input_pin=7;
    s.vehicle_soc_enabled=s.pv_display_enabled=true;s.mqtt_enabled=false;assert(!settings_valid(&s));s.vehicle_soc_enabled=s.pv_display_enabled=false;assert(settings_valid(&s));
    assert(relay_output_level(true,false)==1 && relay_output_level(true,true)==0);
    assert(relay_output_level(false,false)==0 && relay_output_level(false,true)==1);

    /* Legacy ADC selections stop and require commissioning; network data survive. */
    settings_defaults(&s);s.version=10;s.ads1115_enabled=true;s.enabled=true;s.control_verified=true;
    strcpy(s.wifi_ssid,"Existing WiFi");strcpy(s.wallbox_meter_type,"ads1115");
    strcpy(s.house_meter_type,"ads1115");s.grid_guard_enabled=true;
    assert(settings_decode(&s,(offsetof(settings_t,mqtt_input_source)+3)&~(size_t)3,&out));
    assert(!out.ads1115_enabled && !out.control_verified && !out.enabled && out.mode==MODE_OFF);
    assert(!out.grid_guard_enabled && !out.zero_feed_enabled);
    assert(!strcmp(out.wifi_ssid,"Existing WiFi") && !strcmp(out.wallbox_meter_type,"xemex"));
    assert(!strcmp(out.house_meter_type,"tasmota") && settings_valid(&out));
    settings_defaults(&s);strcpy(s.wallbox_meter_type,"ads1115");assert(!settings_valid(&s));
    settings_defaults(&s);strcpy(s.house_meter_type,"ads1115");assert(!settings_valid(&s));
    settings_defaults(&s);s.ads1115_enabled=true;assert(!settings_valid(&s));

    settings_defaults(&s);settings_t runtime=s,next=s;next.relay_board_enabled=true;
    assert(!settings_apply_live(&runtime,&s,&next));
    next=s;next.mqtt_enabled=false;assert(!settings_apply_live(&runtime,&s,&next));
    next=s;next.expert_mode=!s.expert_mode;assert(settings_apply_live(&runtime,&s,&next));
    next=s;next.phase_feedback_enabled=false;assert(!settings_apply_live(&runtime,&s,&next));
    assert(phase_feedback_matches(true,1,1));assert(!phase_feedback_matches(true,3,1));
    assert(phase_feedback_matches(false,0,1));assert(phase_feedback_matches(false,0,3));
    assert(!phase_feedback_matches(false,0,2));
    assert(!phase_motion_complete(false,0,1,1999));assert(phase_motion_complete(false,0,1,2000));
    assert(!phase_motion_complete(false,0,1,-1));assert(!phase_motion_complete(true,3,1,4000));
    assert(phase_motion_complete(true,1,1,0));
    assert(phase_feedback_position(true,true)==1 && phase_feedback_position(false,true)==3);
    assert(phase_feedback_position(true,false)==3 && phase_feedback_position(false,false)==1);
    float reset_idle[]={.1f,.1f,.1f};
    assert(phase_fault_reset_allowed(true,true,true,3,false,true,reset_idle));
    assert(!phase_fault_reset_allowed(false,true,true,3,false,true,reset_idle));
    assert(!phase_fault_reset_allowed(true,false,true,3,false,true,reset_idle));
    assert(!phase_fault_reset_allowed(true,true,true,1,false,true,reset_idle));
    assert(!phase_fault_reset_allowed(true,true,true,3,true,true,reset_idle));
    assert(!phase_fault_reset_allowed(true,true,true,3,false,false,reset_idle));
    reset_idle[0]=1;assert(!phase_fault_reset_allowed(true,true,true,3,false,true,reset_idle));
    reset_idle[0]=NAN;assert(!phase_fault_reset_allowed(true,true,true,3,false,true,reset_idle));
    settings_t old;settings_defaults(&old);old.version=21;old.phase_feedback_enabled=false;
    size_t prefix=offsetof(settings_t,phase_feedback_enabled),length=(prefix+3)&~(size_t)3;
    unsigned char blob[sizeof(old)];memcpy(blob,&old,sizeof(old));memset(blob+prefix,0xff,length-prefix);
    assert(settings_decode(blob,length,&out));assert(out.phase_feedback_enabled);
    old.version=22;prefix=offsetof(settings_t,phase_feedback_closed_is_single);length=(prefix+3)&~(size_t)3;
    memcpy(blob,&old,sizeof(old));memset(blob+prefix,0xff,length-prefix);
    assert(settings_decode(blob,length,&out));assert(!out.phase_feedback_closed_is_single);
    old.version=23;old.phase_feedback_closed_is_single=false;prefix=offsetof(settings_t,control_status_visible);length=(prefix+3)&~(size_t)3;
    memcpy(blob,&old,sizeof(old));memset(blob+prefix,0xff,length-prefix);
    assert(settings_decode(blob,length,&out));assert(out.control_status_visible && !out.phase_feedback_closed_is_single);
    settings_defaults(&s);s.phase_feedback_enabled=false;assert(settings_decode(&s,sizeof(s),&out));assert(!out.phase_feedback_enabled);
    s.phase_feedback_enabled=true;s.phase_feedback_closed_is_single=false;assert(settings_decode(&s,sizeof(s),&out));assert(!out.phase_feedback_closed_is_single);
    s.control_status_visible=false;assert(settings_decode(&s,sizeof(s),&out));assert(!out.control_status_visible);
    next=s;next.control_status_visible=true;runtime=s;runtime.enabled=true;
    assert(settings_apply_live(&runtime,&s,&next));assert(runtime.control_status_visible && runtime.enabled);
    next=s;strcpy(next.wallbox_meter_host,"192.168.1.34");assert(settings_apply_live(&runtime,&s,&next));assert(!strcmp(runtime.wallbox_meter_host,"192.168.1.34"));
    puts("PASS: v10 migration, retired ADC blocked, settings retained, GPIO conflicts, relay polarity/interlocks, hardware restart");
}
