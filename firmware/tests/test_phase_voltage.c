#include "ems_core.h"
#include "phase_voltage.h"
#include "shelly_phase.h"
#include "shelly_modbus.h"
#include <assert.h>
#include <stdio.h>
#include <string.h>

static void put_float(uint8_t *data,unsigned reg,float value){
    uint32_t bits;memcpy(&bits,&value,4);unsigned offset=(reg-SHELLY_EM_START)*2;
    data[offset]=bits>>8;data[offset+1]=bits;data[offset+2]=bits>>24;data[offset+3]=bits>>16;
}
int main(void){
    phase_voltage_proof_t proof={0};float v[3]={230,231,232};
    phase_voltage_sample(&proof,v,true,1000);assert(!phase_voltage_observed(&proof,1000));
    phase_voltage_sample(&proof,v,true,1500);assert(phase_voltage_observed(&proof,1500)==3);
    assert(phase_voltage_observed(&proof,4999)==3&&!phase_voltage_observed(&proof,5000));
    v[1]=v[2]=0;phase_voltage_sample(&proof,v,true,2000);assert(!proof.position);
    phase_voltage_sample(&proof,v,true,2500);assert(phase_voltage_observed(&proof,2500)==1);
    v[1]=230;phase_voltage_sample(&proof,v,true,3000);assert(!phase_voltage_observed(&proof,3000));
    assert(proof.sample_at==3000&&proof.voltage[1]==230); /* Ambiguous raw values remain diagnosable, never a position proof. */
    v[1]=0;v[0]=0;assert(!phase_voltage_position(v));v[0]=NAN;assert(!phase_voltage_position(v));
    v[0]=230;phase_voltage_sample(&proof,v,true,3500);phase_voltage_sample(&proof,v,true,4000);
    assert(proof.position==1);phase_voltage_sample(&proof,v,false,4100);assert(!proof.position&&!proof.at);
    assert(shelly_phase_voltage_json("{\"a_voltage\":0,\"b_voltage\":0,\"c_voltage\":231}",false,v));
    assert(v[0]==231&&v[1]==0&&v[2]==0&&phase_voltage_position(v)==1);
    assert(shelly_phase_voltage_json("{\"emeters\":[{\"voltage\":230},{\"voltage\":231},{\"voltage\":232}]}",true,v));
    assert(v[0]==232&&v[2]==230&&phase_voltage_position(v)==3);
    assert(!shelly_phase_voltage_json("{\"a_voltage\":null,\"b_voltage\":0,\"c_voltage\":230}",false,v));
    assert(!shelly_phase_voltage_json("{\"a_voltage\":0,\"c_voltage\":230}",false,v));
    assert(!shelly_phase_voltage_json("{\"a_voltage\":0,\"b_voltage\":0,\"c_voltage\":230,\"errors\":[\"failure\"]}",false,v));
    uint8_t registers[SHELLY_EM_COUNT*2]={0};shelly_modbus_reading_t reading;
    put_float(registers,1020,230);put_float(registers,1040,0);put_float(registers,1060,231);
    assert(shelly_modbus_decode_em(registers,sizeof(registers),&reading)&&reading.voltage_valid);
    assert(reading.voltage_v[0]==230&&reading.voltage_v[1]==0&&reading.voltage_v[2]==231);
    put_float(registers,1022,NAN);assert(!shelly_modbus_decode_em(registers,sizeof(registers),&reading));
    assert(shelly_modbus_decode_voltage(registers,sizeof(registers),v)&&v[0]==230&&v[2]==231);
    put_float(registers,1022,0);
    put_float(registers,1060,NAN);assert(shelly_modbus_decode_em(registers,sizeof(registers),&reading)&&!reading.voltage_valid);
    settings_t s;settings_defaults(&s);s.basic_mode=false;s.relay_board_enabled=s.phase_switch_enabled=true;s.relay1_mode=3;
    s.phase_feedback_source=2;s.phase_feedback_enabled=false;
    assert(settings_valid(&s));s.phase_no_feedback_acknowledged=false;assert(settings_valid(&s));
    s.phase_shelly_pulse=true;assert(!settings_valid(&s));s.phase_shelly_pulse=false;
    s.phase_feedback_source=1;s.phase_feedback_enabled=true;assert(!settings_valid(&s));
    strcpy(s.phase_voltage_host,"192.168.1.31");assert(settings_valid(&s));
    for(unsigned actuator=0;actuator<4;actuator++){
        s.phase_actuator_type=actuator;strcpy(s.phase_shelly_host,"192.168.1.32");s.phase_shelly_model=4;
        strcpy(s.phase_mqtt_topic,"wallbox-ems/output/single_phase");s.phase_mqtt_output_enabled=true;s.xemex_coils=3;
        assert(settings_valid(&s));
    }
    puts("PASS: reversed 3EM HTTP/Modbus voltage, debounce/stale/partial failures, and source/actuator validation");
}
