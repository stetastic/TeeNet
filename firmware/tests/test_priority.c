#include "ems_core.h"
#include <assert.h>
#include <math.h>
#include <stdio.h>
#include <string.h>
static void near(float a,float b){assert(fabsf(a-b)<0.02f);}
static float car_budget(settings_t *s,float ev,float grid,float charge,float discharge,float soc,bool fresh){
    float a[3]={ev/(s->charge_phases*230),ev/(s->charge_phases*230),ev/(s->charge_phases*230)};
    float adjusted=pv_allocation_grid(s,a,grid,charge,discharge,soc,fresh);
    return ev-adjusted-discharge+s->zero_reserve_w;
}
int main(void){
    settings_t s;settings_defaults(&s);s.basic_mode=false;s.battery_protect=s.zero_feed_enabled=s.pv_allocation_enabled=true;
    assert(settings_valid(&s));
    /* User's example: 8.5 kW PV, 1.28 kW house, 4.83 kW house battery,
       2.39 kW car. Only 7.22 kW belongs to the two charging loads. */
    s.charge_phases=1;s.pv_house_priority_w=4000;
    near(car_budget(&s,2390,0,4830,0,85,true),3220);
    s.pv_priority=1;s.pv_car_priority_w=6000;
    near(car_budget(&s,2390,0,4830,0,85,true),6000);
    s.pv_priority=2;s.pv_house_priority_w=2000;
    near(car_budget(&s,2390,0,4830,0,85,true),5415);
    /* No double counting after the inverter/vehicle settle at a new split. */
    near(car_budget(&s,5415,0,1805,0,85,true),5415);
    /* Import/export and battery discharge are accounted independently. */
    near(car_budget(&s,6000,500,0,3000,85,true),1875);
    near(car_budget(&s,3000,-1000,2000,0,85,true),4500);
    near(car_budget(&s,2390,0,4830,0,100,true),7220);
    near(car_budget(&s,2390,0,4830,0,85,false),2390);
    s.pv_allocation_enabled=false;near(car_budget(&s,2390,0,4830,0,85,true),2390);
    s.pv_allocation_enabled=true;s.pv_priority=0;s.pv_house_priority_w=10000;
    near(car_budget(&s,2390,0,4830,0,85,true),0);
    settings_t before=s,after=s,runtime=s;after.pv_priority=1;after.pv_car_priority_w=6500;
    runtime.enabled=true;runtime.mode=MODE_PV;
    assert(settings_apply_live(&runtime,&before,&after));assert(runtime.enabled&&runtime.mode==MODE_PV);
    near(runtime.pv_car_priority_w,6500);
    after.pv_car_priority_w=NAN;assert(!settings_valid(&after));
    pv_control_t c={0};float measured[3]={10,10,10};
    assert(pv_control_takeover(&c,true,10,measured,3,8,16,10000));near(c.target_a,10);
    pv_control_step(&c,true,10,8,16,11000);near(c.target_a,10);assert(c.phase==PV_RUNNING);
    pv_control_step(&c,true,7,8,16,12000);assert(c.target_a>0&&c.phase==PV_STOPPING);
    for(int t=13000;t<=42000;t+=1000)pv_control_step(&c,true,7,8,16,t);
    assert(c.target_a==0&&c.phase==PV_COOLDOWN);
    memset(&c,0,sizeof(c));assert(!pv_control_takeover(&c,false,10,measured,3,8,16,10000));
    assert(!pv_control_takeover(&c,true,0,measured,3,8,16,10000));
    float idle[3]={.2f,.2f,.2f};assert(!pv_control_takeover(&c,true,10,idle,3,8,16,10000));
    measured[0]=9;measured[1]=measured[2]=0;
    assert(pv_control_takeover(&c,true,10,measured,1,8,16,10000));near(c.target_a,9);
    pv_control_step(&c,false,12,8,16,11000);assert(c.target_a==0); /* phase/fault interlock wins */
    /* Idle EV, 3.4 kW exported and 5.1 kW entering the house battery:
       a 6 kW vehicle ceiling must not wait forever for a 6.7 kW start. */
    settings_defaults(&s);s.basic_mode=false;s.battery_protect=s.zero_feed_enabled=s.pv_allocation_enabled=true;s.battery_reserve_soc=5;
    s.pv_priority=1;s.pv_car_priority_w=6000;
    float adjusted=pv_allocation_grid(&s,idle,-3400,5100,0,18,true);
    float available=battery_solar_current(&s,idle,adjusted,18,true,0,true);
    near(available,s.min_charge_a);near(pv_start_threshold(&s),s.min_charge_a);
    memset(&c,0,sizeof(c));
    for(int t=0;t<=29000;t+=1000)pv_control_step_threshold(&c,true,available,s.min_charge_a,16,pv_start_threshold(&s),t);
    assert(c.target_a==0&&c.phase==PV_STARTING&&c.wait_ms==1000);
    pv_control_step_threshold(&c,true,available,s.min_charge_a,16,pv_start_threshold(&s),30000);
    near(c.target_a,s.min_charge_a);assert(c.phase==PV_RUNNING);
    /* Low actual surplus cannot be made sufficient by the configured ceiling. */
    adjusted=pv_allocation_grid(&s,idle,-3400,0,0,18,true);
    available=battery_solar_current(&s,idle,adjusted,18,true,0,true);
    memset(&c,0,sizeof(c));
    for(int t=0;t<=90000;t+=1000)pv_control_step_threshold(&c,true,available,s.min_charge_a,16,pv_start_threshold(&s),t);
    assert(c.target_a==0&&c.phase==PV_WAITING);
    s.charge_phases=1;near(pv_start_threshold(&s),s.min_charge_a+1);
    memset(&c,0,sizeof(c));available=3400.0f/230;
    for(int t=0;t<=60000;t+=1000)pv_control_step_threshold(&c,true,available,s.min_charge_a,16,pv_start_threshold(&s),t);
    assert(c.target_a>=s.min_charge_a&&c.phase==PV_RUNNING);
    s.charge_phases=3;s.pv_car_priority_w=5500;
    near(pv_start_threshold(&s),s.min_charge_a+1);
    adjusted=pv_allocation_grid(&s,idle,-3400,5100,0,18,true);
    assert(solar_current(&s,idle,adjusted)<s.min_charge_a);
    s.pv_allocation_enabled=false;near(pv_start_threshold(&s),s.min_charge_a+1);
    puts("PASS: PV allocation, bounded minimum-power start, single-phase start, accounting and stop interlocks");
}
