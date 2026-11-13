#include "ems_core.h"
#include <assert.h>
#include <math.h>
#include <stdio.h>

static void near(float a,float b) { assert(fabsf(a-b)<0.001f); }

int main(void) {
    settings_t s; settings_defaults(&s);
    s.grid_limit_a=32; s.grid_guard_enabled=true; s.current_offset_a=0;
    s.enabled=true; s.mode=MODE_MANUAL; s.manual_current_a=16;
    const float house[3]={14,35,22},actual[3]={12,12,12};
    float report[3],expected[3];
    const int64_t measured_at=1000;
    assert(house_phase_currents_ready(true,house,11000,measured_at));
    assert(!house_phase_currents_ready(true,house,11001,measured_at));
    assert(!house_phase_currents_ready(true,house,5000,0));
    assert(!house_phase_currents_ready(true,house,999,measured_at));
    assert(!house_phase_currents_ready(false,house,5000,measured_at));
    assert(!house_phase_currents_ready(true,NULL,5000,measured_at));
    assert(house_phase_currents_ready(true,(float[]){0,0,0},5000,measured_at));
    for(int p=0;p<3;p++) {
        const float invalid[]={NAN,INFINITY,-1,1001};
        for(unsigned i=0;i<sizeof(invalid)/sizeof(invalid[0]);i++) {
            float broken[3]={10,10,10}; broken[p]=invalid[i];
            assert(!house_phase_currents_ready(true,broken,5000,measured_at));
        }
    }
    /* At 230 V, 3 phases: limit the effective request to 8 kW, without
       changing the saved 11 kW request, the minimum or the calibration. */
    float requested=control_target(&s,true,true,true);
    float limited=grid_guard_target(&s,requested,false);
    near(limited*3*230,8000);
    near(s.manual_current_a,16); near(s.min_charge_a,EMS_DEFAULT_MIN_CHARGE_A);
    near(grid_guard_target(&s,8,false),8); /* lower setpoint stays lower */
    near(grid_guard_target(&s,0,false),0);
    near(grid_guard_target(&s,NAN,false),0);
    near(grid_guard_target(&s,-1,false),0);
    s.nominal_v=240; near(grid_guard_target(&s,16,false)*3*240,8000);
    s.nominal_v=230; s.max_charge_a=10;
    near(grid_guard_target(&s,16,false),10); /* configured current ceiling */
    s.max_charge_a=16; s.charge_phases=1;
    near(grid_guard_target(&s,16,false),16); /* one phase remains at <=3.68 kW */
    s.charge_phases=3;
    /* With missing/stale house phases the normal Shell feedback report must
       survive, instead of an artificial overload forcing a stop. */
    control_report(&s,actual,limited,0,false,report);
    for(int p=0;p<3;p++) expected[p]=report[p];
    grid_guard_report(&s,false,(float[]){999,999,999},report);
    for(int p=0;p<3;p++) {near(report[p],expected[p]);assert(report[p]<49);}
    /* New valid phase values restore both the original target and protection
       against the highest real house current. No restart or reset needed. */
    bool ready=house_phase_currents_ready(true,house,12000,12000);
    near(grid_guard_target(&s,requested,ready),16);
    control_report(&s,actual,16,0,false,report);
    grid_guard_report(&s,ready,house,report);
    near(report[0],28); near(report[1],35); near(report[2],28);
    s.grid_guard_enabled=false;
    near(grid_guard_target(&s,16,false),16);
    grid_guard_report(&s,true,(float[]){999,999,999},report);
    near(report[1],35);
    s.grid_guard_enabled=true;
    /* The fallback is only a ceiling: user stop, missing Wallbox feedback,
       missing PV power and anti-cycling latch remain effective. */
    s.enabled=false;
    near(grid_guard_target(&s,control_target(&s,true,true,true),false),0);
    s.enabled=true; s.mode=MODE_OFF;
    near(grid_guard_target(&s,control_target(&s,true,true,true),false),0);
    s.mode=MODE_MANUAL;
    near(grid_guard_target(&s,control_target(&s,false,true,true),false),0);
    near(grid_guard_target(&s,control_target(&s,true,false,true),false),0);
    control_report(&s,actual,0,0,false,report);
    grid_guard_report(&s,false,house,report);
    for(int p=0;p<3;p++) near(report[p],49); /* real stop stays asserted */
    s.mode=MODE_PV; s.pv_surplus_a=9;
    near(grid_guard_target(&s,control_target(&s,true,true,true),false),9);
    s.pv_surplus_a=16;
    near(grid_guard_target(&s,control_target(&s,true,true,true),false)*690,8000);
    near(grid_guard_target(&s,control_target(&s,true,true,false),false),0);
    near(grid_guard_target(&s,fminf(limited,9),false),9); /* EVU cap */
    near(grid_guard_target(&s,0,false),0); /* EVU stop */
    charge_guard_t guard={.latched=true};
    assert(charge_guard_blocked(&guard,15000));
    near(charge_guard_blocked(&guard,15000)?0:limited,0);
    puts("House-phase fallback: 8 kW ceiling, recovery and stop conditions passed");
    return 0;
}
