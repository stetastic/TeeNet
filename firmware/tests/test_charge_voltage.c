#include "charge_voltage.h"
#include <assert.h>
#include <math.h>
#include <stdio.h>
int main(void){
 settings_t s;settings_defaults(&s);strcpy(s.wallbox_meter_type,"shelly_gen2");s.charge_phases=3;
 charge_voltage_t reading={0};float raw[3]={234.7f,235.6f,236.6f},effective[3];
 charge_voltage_sample(&reading,raw,true,4,1000);
 assert(charge_voltage_get(&reading,&s,4,true,1001,15000,effective));
 ems_phase_voltage_override(effective,true);float sum=raw[0]+raw[1]+raw[2];
 assert(fabsf(ems_phase_voltage_sum(&s,3)-sum)<.001f);
 float current=5500/sum;assert(current>7.7f&&current<7.9f);
 float amps[3]={current,current,current};assert(fabsf(estimated_charge_power(&s,amps)-5500)<.02f);
 assert(!charge_voltage_get(&reading,&s,5,true,1001,15000,effective));
 assert(!charge_voltage_get(&reading,&s,4,false,1001,15000,effective));
 assert(!charge_voltage_get(&reading,&s,4,true,16000,15000,effective));
 assert(!charge_voltage_get(&reading,&s,4,true,999,15000,effective));
 strcpy(s.wallbox_meter_type,"xemex");assert(!charge_voltage_get(&reading,&s,4,true,1001,15000,effective));
 strcpy(s.wallbox_meter_type,"shelly_gen2");raw[1]=NAN;charge_voltage_sample(&reading,raw,true,4,2000);assert(!reading.at);
 raw[0]=236.6f;raw[1]=raw[2]=0;charge_voltage_sample(&reading,raw,true,4,3000);
 assert(!charge_voltage_get(&reading,&s,4,true,3001,15000,effective));
 s.charge_phases=1;assert(charge_voltage_get(&reading,&s,4,true,3001,15000,effective));
 assert(effective[0]==236.6f&&effective[1]==230&&effective[2]==230);
 ems_phase_voltage_override(effective,true);assert(fabsf(ems_phase_voltage_sum(&s,1)-236.6f)<.001f);
 charge_voltage_sample(&reading,raw,false,4,4000);assert(!reading.at);
 ems_phase_voltage_override(NULL,false);assert(fabsf(ems_phase_voltage_sum(&s,3)-690)<.001f);
 puts("PASS: selected Shelly voltage, 5.5-kW U/I conversion, source revisions, stale/offline/invalid rejection and one-phase downstream voltages");
}
