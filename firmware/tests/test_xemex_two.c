#include "ems_core.h"
#include <assert.h>
#include <math.h>
#include <stdio.h>
#include <string.h>
int main(void){
    settings_t settings,decoded;settings_defaults(&settings);
    settings.xemex_coils=2;settings.control_verified=true;
    assert(settings_valid(&settings)&&phase_measured_currents(&settings)==2);
    assert(!phase_three_phase_meter(&settings)&&phase_switch_meter_supported(&settings));
    float current[3]={10,10,NAN};assert(reconstruct_currents(current,2));assert(current[2]==10);
    assert(fabsf(estimated_charge_power(&settings,current)-6900)<.01f);
    settings.charge_phases=1;current[0]=8;current[1]=0;current[2]=NAN;
    assert(reconstruct_currents(current,2)&&current[2]==0&&!wallbox_inactive_phase_current(&settings,current));
    assert(fabsf(estimated_charge_power(&settings,current)-1840)<.01f);
    current[1]=2;assert(reconstruct_currents(current,2)&&wallbox_inactive_phase_current(&settings,current));
    current[0]=current[1]=0;current[2]=NAN;assert(phase_measured_zero(&settings,current,1));
    current[1]=2;assert(!phase_measured_zero(&settings,current,1));
    current[1]=NAN;assert(!phase_measured_zero(&settings,current,1));
    current[1]=-.1f;assert(!phase_measured_zero(&settings,current,1));
    settings.xemex_coils=3;current[1]=0;assert(!phase_measured_zero(&settings,current,1));
    settings.xemex_coils=2;settings.phase_switch_enabled=settings.relay_board_enabled=true;settings.relay1_mode=3;
    settings.phase_actuator_type=2;settings.phase_feedback_enabled=false;
    assert(settings_valid(&settings)&&phase_output_settings_supported(&settings));
    settings.xemex_coils=1;assert(!phase_output_settings_supported(&settings));settings.xemex_coils=2;
    assert(settings_decode(&settings,sizeof(settings),&decoded)&&decoded.xemex_coils==2&&decoded.control_verified);
    settings.phase_actuator_type=1;settings.phase_feedback_enabled=true;strcpy(settings.phase_shelly_host,"192.168.1.88");
    assert(settings_valid(&settings));settings.phase_actuator_type=3;settings.phase_shelly_model=4;assert(settings_valid(&settings));
    strcpy(settings.house_meter_type,"xemex");settings.house_xemex_coils=2;assert(!settings_valid(&settings));
    puts("PASS: two Xemex coils: real L1/L2, explicit estimated L3=L2, correct 1/3-phase power, real L2 blocks 1-phase charging, measured-only zero gate, expert/RPC/Modbus permission and reboot persistence; house meter still needs three real phases");
}
