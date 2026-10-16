#include "ems_features.h"
#include "ems_core.h"
#include <assert.h>
#include <math.h>
#include <stdio.h>
#include <string.h>

static void plans(void) {
    const int64_t now=1791000000;
    settings_t s;settings_defaults(&s);s.grid_guard_enabled=true;
    assert(fabsf(charge_plan_maximum_kw(&s,false,false)-8)<.001f);
    assert(fabsf(charge_plan_maximum_kw(&s,true,false)-11.04f)<.001f);
    charge_plan_t limited={0},unlimited={0};
    assert(charge_plan_start(&limited,8,now+3800,now,0));unlimited=limited;
    assert(charge_plan_step(&limited,now,true,true,0,charge_plan_maximum_kw(&s,false,false))==PLAN_GRID);
    assert(charge_plan_step(&unlimited,now,true,true,0,charge_plan_maximum_kw(&s,true,false))==PLAN_PV);
    s.evu_input_enabled=true;s.evu_limit_a=9;
    assert(fabsf(charge_plan_maximum_kw(&s,false,true)-6.21f)<.001f);
    s.evu_limit_a=0;assert(charge_plan_maximum_kw(&s,false,true)==0);
    charge_plan_t p={0};
    assert(!charge_plan_start(&p,NAN,now+3600,now,0));
    assert(!charge_plan_start(&p,.49f,now+3600,now,0));
    assert(!charge_plan_start(&p,101,now+3600,now,0));
    assert(!charge_plan_start(&p,20,now,now,0));
    assert(!charge_plan_start(&p,20,now+8*86400,now,0));
    assert(!charge_plan_start(&p,20,now+3600,0,0));
    assert(!charge_plan_start(&p,20,now+3600,now,NAN));
    assert(charge_plan_start(&p,20,now+4*3600,now,120000));
    assert(charge_plan_step(&p,now+1,true,true,120000,11)==PLAN_PV);
    assert(charge_plan_step(&p,now+3600,true,true,125000,11)==PLAN_PV);
    assert(p.delivered_wh==5000 && !p.grid);
    /* A paused plan neither starts nor assigns unrelated energy to itself. */
    assert(charge_plan_step(&p,now+3601,true,false,126000,11)==PLAN_PAUSED);
    assert(p.delivered_wh==5000);
    assert(charge_plan_step(&p,now+3602,true,true,126500,11)==PLAN_PV);
    assert(p.delivered_wh==5500);
    assert(charge_plan_step(&p,0,false,true,126500,11)==PLAN_CLOCK);
    assert(charge_plan_step(&p,now+4*3600-4500,true,true,126500,11)==PLAN_GRID);
    /* A clock correction cannot oscillate back from grid completion into PV. */
    assert(charge_plan_step(&p,now+4000,true,true,126500,11)==PLAN_GRID);
    assert(charge_plan_step(&p,now+5000,true,true,141000,11)==PLAN_COMPLETE);
    assert(!p.active && p.delivered_wh==20000);
    assert(charge_plan_step(&p,now+6*3600,true,false,141000,11)==PLAN_COMPLETE);
    assert(charge_plan_start(&p,5,now+1800,now,0));
    assert(charge_plan_step(&p,now+1800,true,true,1000,11)==PLAN_EXPIRED);
    assert(!p.active && p.delivered_wh==1000);
    /* Missing/zero/invalid maximum power cannot divide by zero or request grid. */
    assert(charge_plan_start(&p,5,now+3600,now,0));
    assert(charge_plan_step(&p,now+1,true,true,0,0)==PLAN_PAUSED);
    assert(charge_plan_step(&p,now+2,true,true,0,NAN)==PLAN_PAUSED);
    assert(charge_plan_step(&p,now+3,true,true,NAN,11)==PLAN_PV);
    /* Restored cumulative energy and a reset counter do not double count. */
    p.last_total_wh=10000;
    assert(charge_plan_step(&p,now+4,true,true,5000,11)==PLAN_PV);
    assert(p.delivered_wh==0);
    assert(charge_plan_step(&p,now+5,true,true,5500,11)==PLAN_PV);
    assert(p.delivered_wh==500);
    assert(charge_plan_phase_name(PLAN_GRID)[0]=='g');
}
static void events(void) {
    ems_events_t log={0};
    assert(!ems_event_recent(&log,0));
    ems_event_observe(&log,"meter",0,10,8,0,NAN,NAN);
    ems_event_observe(&log,"meter",0,11,8,0,NAN,NAN);
    assert(log.count==1);
    ems_event_observe(&log,"none",0,11,8,8,0,70);
    assert(log.count==1);
    ems_event_observe(&log,"none",0,12,8,8,0,70);
    assert(log.count==2);
    for(int i=0;i<120;i++)ems_event_add(&log,"control_changed",1791000000+i,100+i,8,8,-1,70);
    assert(log.count==48);
    assert(ems_event_recent(&log,0)->sequence==122);
    assert(ems_event_recent(&log,47)->sequence==75);
    assert(!ems_event_recent(&log,48));
    ems_event_add(&log,"abcdefghijklmnopqrstuvwxyz0123456789",0,999,0,0,0,0);
    assert(strlen(ems_event_recent(&log,0)->reason)==31);
}
static void pv_replay(void) {
    settings_t s,out;settings_defaults(&s);assert(!s.charge_plan_enabled);
    s.version=17;s.charge_plan_enabled=true;
    assert(settings_decode(&s,(offsetof(settings_t,charge_plan_enabled)+3)&~(size_t)3,&out));
    assert(!out.charge_plan_enabled && out.min_charge_a==8.7f && out.max_charge_a==16);
    settings_defaults(&s);s.basic_mode=false;settings_t runtime=s,next=s;runtime.enabled=true;runtime.mode=MODE_MANUAL;
    next.charge_plan_enabled=true;assert(settings_apply_live(&runtime,&s,&next));
    assert(runtime.charge_plan_enabled && runtime.enabled && runtime.mode==MODE_MANUAL);
    s=next;next.charge_plan_enabled=false;assert(settings_apply_live(&runtime,&s,&next));
    assert(!runtime.charge_plan_enabled && runtime.enabled && runtime.mode==MODE_MANUAL);
    settings_defaults(&s);s.control_verified=true;s.enabled=true;s.mode=MODE_PV;
    s.zero_feed_enabled=true;s.battery_protect=true;s.battery_reserve_soc=50;
    s.grid_limit_a=32;
    pv_control_t pv={0};battery_buffer_t buffer={0};
    const float amps[3]={10,10,10};
    for(int64_t t=0;t<=70000;t+=1000)pv_control_step(&pv,true,12,8,16,t);
    assert(pv.target_a>=8);
    /* Replay a cloud with 3 kW grid deficit and fresh battery feedback. */
    for(int64_t t=71000;t<971000;t+=1000){
        float available=battery_cloud_current(&buffer,&s,amps,0,70,true,3000,true,true,true,8,t);
        assert(available>=8 && buffer.active);
        pv_control_step(&pv,true,available,8,16,t);
        assert(pv.target_a>=8 && pv.target_a<=16);
    }
    float expired=battery_cloud_current(&buffer,&s,amps,0,70,true,3000,true,true,true,8,971000);
    assert(expired<8 && !buffer.active);
    /* Stale SoC and reserve disallow support, while keeping genuine PV. */
    memset(&buffer,0,sizeof(buffer));
    assert(battery_cloud_current(&buffer,&s,amps,0,70,false,3000,true,true,true,8,1000000)<8);
    assert(battery_cloud_current(&buffer,&s,amps,0,49,true,3000,true,true,true,8,1001000)<8);
    assert(battery_cloud_current(&buffer,&s,amps,0,70,true,3000,true,false,true,8,1002000)<8);
    /* A WiFi or meter failure bypasses all cloud/stop delays. */
    pv_control_step(&pv,false,16,8,16,972000);assert(pv.target_a==0);
    for(int64_t t=973000;t<1003000;t+=1000){pv_control_step(&pv,true,16,8,16,t);assert(pv.target_a==0);}
    pv_control_step(&pv,true,16,8,16,1003000);assert(pv.target_a>=8);
    /* A short household load increase does not chatter the contactor. */
    for(int64_t t=1004000;t<1104000;t+=1000){pv_control_step(&pv,true,(t/10000)%2?7.2f:12,8,16,t);assert(pv.target_a>=8);}
    pv_control_step(&pv,true,NAN,8,16,1104000);assert(pv.target_a==0);
    /* Settings and minimum/ramp implementation have not been retuned. */
    assert(s.min_charge_a==8.7f && s.max_charge_a==16);
}
int main(void){plans();events();pv_replay();puts("PASS: PV-first plans, deadline/grid completion, pause, clock/counter faults, bounded event log, 15-minute cloud replay, stale battery/meter and recovery");return 0;}
