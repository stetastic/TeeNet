#include "ems_core.h"
#include <assert.h>
#include <math.h>
#include <stdio.h>
#include <string.h>
static void near(float a,float b){assert(fabsf(a-b)<.002f);}
int main(void){
    settings_t s;settings_defaults(&s);s.battery_protect=s.zero_feed_enabled=true;
    s.battery_reserve_soc=50;s.battery_cloud_limit_w=s.battery_assist_limit_w=4000;
    s.charge_phases=1;s.min_charge_a=8.7f;
    const float idle[3]={0,0,0},amps[3]={8.7f,0,0};
    battery_buffer_t b={0};
    /* SOC limits prohibit discharge, not genuine solar charging. */
    near(battery_support_current(&b,&s,idle,-2300,50,true,0,true,true,true,false,8.7f,1000),10);
    near(battery_support_current(&b,&s,idle,-2300,80,false,0,true,true,true,false,8.7f,1000),10);
    /* Start with no PV, even with both independent helpers enabled. */
    near(battery_support_current(&b,&s,idle,0,80,true,0,true,true,true,false,8.7f,1000),16);
    assert(!b.started);
    /* Delayed inverter response has a bounded response interval. */
    near(battery_support_current(&b,&s,amps,2001,80,true,0,true,true,false,true,8.7f,2000),8.7f);
    assert(b.active&&b.remaining_ms==900000);
    near(battery_support_current(&b,&s,amps,2001,80,true,0,true,true,false,true,8.7f,2000+EMS_BATTERY_RESPONSE_MS-1),8.7f);
    assert(b.remaining_ms==EMS_BATTERY_BUFFER_MS-EMS_BATTERY_RESPONSE_MS+1);
    near(battery_support_current(&b,&s,amps,2001,80,true,0,true,true,false,true,8.7f,2000+EMS_BATTERY_RESPONSE_MS),0);
    /* Recorded EV start: below-target current is not a failed inverter when
       measured discharge and a near-zero grid balance confirm coverage. */
    memset(&b,0,sizeof(b));const float ramping[3]={7.4f,0,0};
    for(int64_t t=1000;t<=90000;t+=1000) {
        near(battery_support_current(&b,&s,ramping,78,80,true,312,true,true,true,true,8.7f,t),8.7f);
        assert(b.active&&b.cloud_response_at==0&&b.assist_response_at==0);
    }
    /* Small discharge for household loads does not authorize persistent
       vehicle grid charging; this still loses support after the interval. */
    for(int64_t t=91000;t<91000+EMS_BATTERY_RESPONSE_MS;t+=1000)
        near(battery_support_current(&b,&s,ramping,1600,80,true,312,true,true,true,true,8.7f,t),8.7f);
    assert(battery_support_current(&b,&s,ramping,1600,80,true,312,true,true,true,true,8.7f,91000+EMS_BATTERY_RESPONSE_MS)<8.7f);
    /* When the battery supplies the load, buffer reaches exactly 15 min.
       A brief missing sample, sunny gap or phase pause cannot refill it. */
    memset(&b,0,sizeof(b));
    for(int64_t t=1000;t<901000;t+=1000){
        bool power_ok=t!=60000,charging=t<100000||t>105000;
        float discharge=t>=200000&&t<210000?0:2001;
        float value=battery_support_current(&b,&s,amps,0,80,true,discharge,power_ok,true,false,charging,8.7f,t);
        assert(b.started&&b.remaining_ms==901000-t);
        if(power_ok&&charging)assert(value>=8.7f);
    }
    near(battery_support_current(&b,&s,amps,0,80,true,2001,true,true,false,true,8.7f,901000),0);
    assert(!b.active&&b.started&&b.remaining_ms==0);
    /* Assist remains independent even after cloud allowance expires. */
    near(battery_support_current(&b,&s,amps,0,80,true,2001,true,true,true,true,8.7f,902000),8.7f);
    assert(b.assist_active&&!b.active&&b.remaining_ms==0);
    near(battery_support_current(&b,&s,amps,0,50,true,2001,true,true,true,true,8.7f,903000),0);
    for(int64_t t=904000;t<=964000;t+=1000)battery_support_current(&b,&s,amps,-230,80,true,0,true,true,false,true,8.7f,t);
    assert(!b.started); /* one full stable PV minute rearms the timer */
    /* Re-arming needs continuous PV operation, not a sunny sample on either
       side of a long pause, missing input interval or unavailable battery. */
    b=(battery_buffer_t){.started=true,.started_at=1000};
    battery_support_current(&b,&s,amps,-230,80,true,0,true,true,false,true,8.7f,10000);
    assert(b.recovery_at==10000);
    battery_support_current(&b,&s,idle,-2300,80,true,0,true,true,false,false,8.7f,20000);
    battery_support_current(&b,&s,amps,-230,80,true,0,true,true,false,true,8.7f,80000);
    assert(b.started&&b.recovery_at==80000);
    for(int64_t t=81000;t<=140000;t+=1000)
        battery_support_current(&b,&s,amps,-230,80,true,0,true,true,false,true,8.7f,t);
    assert(!b.started);
    b=(battery_buffer_t){.started=true,.started_at=1000};
    battery_support_current(&b,&s,amps,-230,80,true,0,true,true,false,true,8.7f,10000);
    battery_support_current(&b,&s,amps,-230,80,true,0,true,true,false,true,8.7f,80000);
    assert(b.started&&b.recovery_at==80000);
    battery_support_current(&b,&s,amps,-230,80,false,0,true,true,false,true,8.7f,81000);
    assert(b.recovery_at==0);
    /* MQTT direction changes never count simultaneous charge/discharge. */
    battery_power_t p=battery_power_normalize(2000,2500,1100,1000,1200,30000);
    assert(p.fresh&&p.conflicting&&p.discharge_w==0&&p.charge_w==2000);
    p=battery_power_normalize(2000,2500,1000,1100,1200,30000);assert(p.discharge_w==2500&&p.charge_w==0);
    p=battery_power_normalize(2000,2500,1100,1100,1200,30000);assert(p.discharge_w==500);
    p=battery_power_normalize(0,2000,1000,1000,31001,30000);assert(!p.fresh);
    /* Persist deadlines across OTA/reboot without starting a charge. */
    pv_control_t pv={.initialized=true,.stopped=true,.stopped_at=100000,.last_at=101000};
    b=(battery_buffer_t){.started=true,.started_at=1000};control_memory_t m;
    const int64_t utc=1791720000000LL;
    control_memory_capture(&m,&b,&pv,true,true,101000,utc);
    assert(control_memory_valid(&m,sizeof(m))&&m.cloud_ms==800000&&m.cooldown_ms==119000);
    control_memory_restore(&m,&b,&pv,5000,utc+60000);
    assert(b.started&&b.remaining_ms==740000&&pv.target_a==0&&pv.wait_ms==59000);
    assert(EMS_BATTERY_BUFFER_MS-(5000-b.started_at)==740000);
    control_memory_restore(&m,&b,&pv,5000,utc+1000000);assert(b.started&&b.remaining_ms==0&&!pv.stopped);
    control_memory_restore(&m,&b,&pv,5000,0);assert(b.started&&b.remaining_ms==0&&pv.target_a==0);
    expert_values_t expert;expert_defaults(&expert);
    expert.values[EP_BAT_BUFFER]=60;expert.values[EP_PV_COOLDOWN]=30;assert(expert_activate(&expert));
    control_memory_restore(&m,&b,&pv,5000,utc+1000);
    assert(b.remaining_ms==60000&&pv.wait_ms==30000&&b.started_at==5000&&pv.stopped_at==5000);
    expert_defaults(&expert);assert(expert_activate(&expert));
    m.flags^=1;assert(!control_memory_valid(&m,sizeof(m)));
    /* PV start delay runs during cooldown, not after it. */
    memset(&pv,0,sizeof(pv));
    for(int64_t t=1000;t<=31000;t+=1000)pv_control_step(&pv,true,12,8.7f,16,t);
    assert(pv.target_a>0);
    for(int64_t t=32000;t<=62000;t+=1000)pv_control_step(&pv,true,0,8.7f,16,t);
    assert(pv.target_a==0&&pv.wait_ms==120000);
    for(int64_t t=63000;t<182000;t+=1000)pv_control_step(&pv,true,12,8.7f,16,t);
    pv_control_step(&pv,true,12,8.7f,16,182000);assert(pv.target_a>0);
    puts("PASS: PV at reserve, independent helpers, delayed feedback, exact 15-minute cloud deadline, telemetry transitions, reboot/OTA deadlines, no double cooldown");
}
