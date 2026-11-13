#include "meter_hold.h"
#include <assert.h>
#include <stdio.h>
int main(void) {
 settings_t s;settings_defaults(&s);s.enabled=true;s.mode=MODE_MANUAL;
 meter_hold_t h={0};float amps[3]={9,9,9},report[3]={24,36,34};
 assert(!meter_hold_active(&h,&s,true,true,10000,5000));
 meter_hold_sample(&h,&s,true,true,1000,1000,amps,9);
 assert(!meter_hold_active(&h,&s,true,true,5999,1000));
 assert(meter_hold_active(&h,&s,true,true,6000,1000));
 assert(meter_hold_active(&h,&s,true,true,60999,1000));
 assert(!meter_hold_active(&h,&s,true,true,61000,1000));
 assert(!meter_hold_active(&h,&s,false,true,20000,1000));
 assert(!meter_hold_active(&h,&s,true,false,20000,1000));
 assert(!meter_hold_active(&h,&s,true,true,20000,2000));
 meter_hold_report(&s,true,9,report);
 for(unsigned i=0;i<3;i++)assert(report[i]==s.grid_limit_a);
 s.charge_phases=1;assert(!meter_hold_active(&h,&s,true,true,20000,1000));
 meter_hold_report(&s,true,9,report);assert(report[0]==s.grid_limit_a&&report[1]==0&&report[2]==0);
 control_report(&s,amps,0,0,false,report);float stop=report[0];meter_hold_report(&s,true,0,report);assert(report[0]==stop);
 s.enabled=false;assert(!meter_hold_active(&h,&s,true,true,20000,1000));
 meter_hold_sample(&h,&s,true,true,21000,1000,amps,0);assert(!h.sample_at);
 s.enabled=true;amps[0]=amps[1]=amps[2]=0;
 meter_hold_sample(&h,&s,true,true,22000,22000,amps,9);assert(!meter_hold_active(&h,&s,true,true,30000,22000));
 puts("PASS: measured-flow prerequisite, 5–60 s neutral hold, expiry, no new start, recovery, phase change and Stop override");
}
