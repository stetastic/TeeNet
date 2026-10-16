#include "ems_core.h"
#include <assert.h>
#include <math.h>
#include <stdio.h>
#include <string.h>
static void near(float a,float b) { if(fabsf(a-b)>=0.0001f){fprintf(stderr,"Mismatch: %.6f != %.6f\n",a,b);assert(0);} }
static const float charging[3]={8.1f,8.1f,8.1f},idle[3]={.2f,.2f,.2f};
static void confirmed_stop(charge_guard_t *g,int64_t at) {
    for(int i=0;i<=10;i++)charge_guard_step(g,true,true,charging,8,at+i*1000);
    assert(g->seen_charging);
    for(int i=11;i<=32;i++) charge_guard_step(g,true,true,idle,8,at+i*1000);
}
int main(void) {
    settings_t saved,runtime,next;
    settings_defaults(&saved); saved.basic_mode=false;saved.grid_limit_a=32;
    near(estimated_charge_power(&saved,idle),0);
    near(estimated_charge_power(&saved,(float[]){8,8,8}),5520);
    assert(isnan(estimated_charge_power(&saved,(float[]){0,NAN,0})));
    for(int mode=MODE_OFF;mode<=MODE_PV;mode++) {
        runtime=saved; runtime.mode=mode; runtime.enabled=mode!=MODE_OFF;
        runtime.manual_current_a=7.3f; runtime.pv_surplus_a=11.7f;
        next=saved; next.price_kwh=.44f; next.solar_price_kwh=.09f;
        settings_t expected=runtime; expected.price_kwh=next.price_kwh; expected.solar_price_kwh=next.solar_price_kwh;
        assert(settings_apply_live(&runtime,&saved,&next));
        assert(!memcmp(&runtime,&expected,sizeof(runtime)));
        assert(settings_apply_live(&runtime,&next,&next)); /* identical full form */
        next.grid_limit_a=25;
        assert(settings_apply_live(&runtime,&saved,&next));
        expected.grid_limit_a=25;
        assert(!memcmp(&runtime,&expected,sizeof(runtime)));
        next=saved; next.wifi_ssid[0]='X'; assert(!settings_apply_live(&runtime,&saved,&next));
        assert(!memcmp(&runtime,&expected,sizeof(runtime))); /* failed apply is atomic */
        next=saved; next.current_offset_a=.5f; assert(!settings_apply_live(&runtime,&saved,&next));
    }
    /* Changing the discharge depth must not turn an active charge off. */
    settings_defaults(&saved); saved.basic_mode=false;saved.grid_limit_a=32;
    runtime=saved; runtime.mode=MODE_PV; runtime.enabled=true;
    next=saved; next.battery_reserve_soc=25;
    assert(settings_apply_live(&runtime,&saved,&next));
    assert(runtime.mode==MODE_PV && runtime.enabled && runtime.battery_reserve_soc==25);
    saved=next; next.battery_protect=true;
    assert(settings_apply_live(&runtime,&saved,&next));
    assert(runtime.enabled && runtime.battery_protect);
    saved=next; next.battery_protect=false;
    assert(settings_apply_live(&runtime,&saved,&next));
    assert(runtime.enabled && !runtime.battery_protect);
    saved=next;
    next.battery_cloud_limit_w=2500;next.battery_assist_limit_w=3000;next.homeassistant_enabled=true;next.mqtt_input_source=1;
    assert(settings_apply_live(&runtime,&saved,&next));
    near(runtime.battery_cloud_limit_w,2500);near(runtime.battery_assist_limit_w,3000);assert(runtime.homeassistant_enabled);assert(runtime.mqtt_input_source==1);
    saved=next;
    /* Migration preserves the same internal request: old 7.3 -> desired 8.1.
       Loading v7 a second time must not add the offset again. */
    saved.version=6; saved.min_charge_a=7.1f; saved.manual_current_a=7.3f;
    saved.mode=MODE_MANUAL; saved.enabled=true;
    assert(settings_decode(&saved,offsetof(settings_t,current_offset_a),&next));
    near(next.min_charge_a,8); near(next.manual_current_a,8.1f);
    near(next.current_offset_a,.8f); assert(!next.enabled);
    assert(settings_decode(&next,sizeof(next),&runtime)); near(runtime.manual_current_a,8.1f);
    runtime.enabled=true; float out[3],old[3];
    next=runtime; next.current_offset_a=0; next.min_charge_a=7.1f;
    control_report(&runtime,charging,8.1f,0,false,out);
    control_report(&next,charging,7.3f,0,false,old);
    near(out[0],32.8f); near(old[0],32.8f); near(charging[0],8.1f);
    /* Legacy migration keeps the earlier 8 A request consistent. Current
       installations normalize the proven three-phase floor to 8.7 A. */
    float minimum_actual[3]={8,8,8};
    control_report(&runtime,minimum_actual,8,0,false,out); near(out[0],32.8f);
    near(control_target(&runtime,true,true,true),8.1f);
    runtime.manual_current_a=7.9f; near(control_target(&runtime,true,true,true),0);
    control_report(&runtime,idle,0,0,false,out); near(out[0],49);

    /* Switching an experimental feature off must recover from a phase fault
       while stopped; enabling must still require healthy three-phase feedback. */
    assert(phase_option_change_allowed(true,false,1,false,true,idle));
    assert(phase_option_change_allowed(true,false,3,false,true,idle));
    assert(!phase_option_change_allowed(false,true,3,false,true,idle));
    assert(!phase_option_change_allowed(false,true,1,true,true,idle));
    assert(phase_option_change_allowed(false,true,3,true,true,idle));
    assert(!phase_option_change_allowed(true,false,1,false,false,idle));
    float charging_now[3]={1.2f,0,0};
    assert(!phase_option_change_allowed(true,false,1,false,true,charging_now));

    settings_t pinned;saved.control_verified=true;pinned=saved;
    pinned.xemex_tx_pin=8;pinned.relay2_pin=9;
    settings_fixed_pins(&pinned);
    assert(pinned.xemex_tx_pin==8 && pinned.xemex_rx_pin==5 && pinned.wallbox_tx_pin==17 && pinned.wallbox_rx_pin==18);
    assert(pinned.house_tx_pin==43 && pinned.house_rx_pin==44 && pinned.relay2_pin==14 && pinned.control_verified);
    charge_guard_t g={0};
    /* No car: even a long-lived start request must not create a stop/restart loop. */
    for(int64_t t=1000;t<600000;t+=1000) charge_guard_step(&g,true,true,idle,8,t);
    assert(!g.latched && !g.stop_count && !g.retry_until);
    memset(&g,0,sizeof(g));
    confirmed_stop(&g,1000); assert(!g.latched && g.stop_count==1);
    assert(charge_guard_blocked(&g,61000)); assert(!charge_guard_blocked(&g,62000));
    confirmed_stop(&g,70000); assert(!g.latched && g.stop_count==2);
    confirmed_stop(&g,140000); assert(!g.latched && g.stop_count==3);
    confirmed_stop(&g,210000); assert(!g.latched && g.stop_count==4);
    confirmed_stop(&g,280000); assert(g.latched && g.stop_count==5);
    charge_guard_step(&g,false,true,idle,8,320000); assert(g.latched);
    assert(charge_guard_blocked(&g,999999)); /* only explicit restart clears it */
    memset(&g,0,sizeof(g));
    confirmed_stop(&g,1000);
    confirmed_stop(&g,340000); assert(!g.latched && g.stop_count==1); /* rolling window */
    memset(&g,0,sizeof(g));
    charge_guard_step(&g,true,true,charging,8,1000);
    charge_guard_step(&g,false,true,idle,8,2000); /* commanded stop */
    for(int t=3000;t<120000;t+=1000) charge_guard_step(&g,false,true,idle,8,t);
    assert(!g.stop_count);
    charge_guard_step(&g,true,true,charging,8,120000);
    charge_guard_step(&g,true,true,idle,8,121000);
    charge_guard_step(&g,true,false,idle,8,124000); /* stale sample resets confirmation */
    charge_guard_step(&g,true,true,charging,8,125000);
    assert(!g.stop_count && !g.low_since);
    /* A Shell pause shorter than 20 s must not trigger a restart. */
    memset(&g,0,sizeof(g));
    for(int t=1000;t<=10000;t+=1000) charge_guard_step(&g,true,true,charging,8,t);
    for(int t=11000;t<31000;t+=1000) charge_guard_step(&g,true,true,idle,8,t);
    assert(!g.stop_count && !g.latched);
    charge_guard_step(&g,true,true,charging,8,31000);
    assert(!g.stop_count && !g.low_since);
    memset(&g,0,sizeof(g));
    charge_guard_step(&g,true,true,charging,8,1000);
    for(int t=2000;t<30000;t+=1000)charge_guard_step(&g,true,true,idle,8,t);
    assert(!g.stop_count&&!g.latched); /* short current with no car is not a charge */
    settings_defaults(&saved);saved.mode=MODE_MANUAL;
    manual_report_filter_t filter={0};float report[3]={24,24,24};
    manual_report_smooth(&filter,&saved,8,1000,report);near(report[0],24);
    report[0]=report[1]=report[2]=32;
    manual_report_smooth(&filter,&saved,8,2000,report);near(report[0],24.05f);
    report[0]=report[1]=report[2]=32;
    manual_report_smooth(&filter,&saved,11,3000,report);near(report[0],24.45f);
    report[0]=report[1]=report[2]=32;
    manual_report_smooth(&filter,&saved,14,4000,report);near(report[0],24.65f);
    report[0]=report[1]=report[2]=20;
    manual_report_smooth(&filter,&saved,8,5000,report);near(report[0],24.55f);
    report[0]=report[1]=report[2]=20;
    manual_report_smooth(&filter,&saved,11,6000,report);near(report[0],23.75f);
    report[0]=report[1]=report[2]=20;
    manual_report_smooth(&filter,&saved,14,7000,report);near(report[0],23.35f);
    report[0]=report[1]=report[2]=23.2f;
    manual_report_smooth(&filter,&saved,14,8000,report);near(report[0],23.2f);
    report[0]=report[1]=report[2]=47;
    manual_report_smooth(&filter,&saved,0,9000,report);near(report[0],47);assert(!filter.active);
    puts("PASS: tariff without charging interruption, v6/v7 calibration, idle without car, retry and rolling anti-cycle lock");
}
