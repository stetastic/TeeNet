/* Closed-loop replay with the real EMS code and an explicitly simulated EV /
   inverter. Does not write MQTT values or drive physical relay outputs. */
#include "ems_core.h"
#include <assert.h>
#include <math.h>
#include <stdio.h>
#include <string.h>
typedef struct {const char *name;int kind,seconds;unsigned phases;bool cloud,assist;int priority;} profile_t;
static unsigned rng=72419;
static float random_unit(void){rng=rng*1664525u+1013904223u;return (rng>>8)/16777216.0f;}
static void run(profile_t p){
    settings_t s;settings_defaults(&s);s.charge_phases=p.phases;s.min_charge_a=8.7f;
    s.battery_protect=s.zero_feed_enabled=true;s.battery_reserve_soc=40;
    s.pv_allocation_enabled=p.priority>=0;s.pv_priority=p.priority<0?1:p.priority;
    s.pv_house_priority_w=1500;s.pv_car_priority_w=p.priority==2?3500:6000;
    battery_buffer_t buffer={0};pv_control_t pv={0};
    float amps=0,charge=0,discharge=0,soc=70,delayed_target[12]={0};
    int starts=0,stops=0,first_start=-1,first_stop=-1;bool running=false;
    float peak_grid=0,final_ev=0,final_charge=0,final_grid=0;double imported_wh=0;
    for(int t=1;t<=p.seconds;t++){
        int64_t now=(int64_t)t*1000;
        float solar=10000,house=500;bool soc_ok=true,power_ok=true,meter_ok=true;
        switch(p.kind){
        case 0:solar=fminf(10000,500+t*15);house=600;break;
        case 1:solar=t<=120?4000:0;house=300;break;
        case 2:solar=t<120||t%90>=15?10000:1800;break;
        case 3:house=t<120||t%120>=10?500:6500;break;
        case 4:solar=12000;house=t>=120&&t<420?9000:500;break;
        case 5:solar=3000;if(t>=600)soc=39;break;
        case 6:if(t>=120&&t<180)soc_ok=false;break;
        case 7:if(t>=120&&t<130)power_ok=false;break;
        case 8:if(t>=120&&t<170)meter_ok=false;break;
        case 10:solar=5000;house=600;break;
        case 11:case 12:case 13:solar=5000;house=600;break;
        case 14:solar=5000;house=600;soc=99;break;
        case 15:solar=t>=300&&t<600?0:5000;house=600;break;
        case 16:solar=0;house=500;break;
        case 17:solar=t<400?0:10000;house=500;break;
        case 18:solar=0;house=500;break; /* delayed inverter + slow EV start */
        case 19:solar=5000;house=600;break; /* battery charging with cloud only */
        case 20:solar=0;house=500;if(t>=300)soc=39;break;
        case 21:solar=0;house=500;if(t>=120&&t<180)soc_ok=false;break;
        case 22:solar=3000;house=600;if(t>=300)soc=99;break;
        case 9:{
            float daylight=sinf((t%86400)/86400.0f*6.2831853f-1.5707963f);
            solar=fmaxf(0,11000*daylight);house=600+random_unit()*200;
            if(t%600<12)solar*=.15f;
            if(t%900<18)house+=6500;
            if(t%3600<9)power_ok=false;
            if(t%7200<7)meter_ok=false;
            break;}
        }
        /* EV follows the requested current after 7 s, with a 1 A/s slew.
           Inverter needs 10 s to approach a zero-grid-power operating point.
           Neither model is a prediction for a particular vehicle. */
        float desired=delayed_target[t%12];
        float slew=p.kind==18?.35f:1;
        amps+=fmaxf(-slew,fminf(slew,desired-amps));
        float actual[3]={amps,p.phases==3?amps:0,p.phases==3?amps:0};
        float ev=estimated_charge_power(&s,actual);
        float need=house+ev-solar;
        float want_discharge=soc>15?fminf(4000,fmaxf(0,need)):0;
        if(p.kind==16||p.kind==17)want_discharge=0; /* configured inverter refuses discharge */
        float want_charge=soc<99?fminf(4000,fmaxf(0,-need)):0;
        discharge+=(want_discharge-discharge)/(p.kind==18?20:10);
        charge+=(want_charge-charge)/10;
        battery_power_t bp=battery_power_normalize(charge,discharge,now,now,now,EMS_INPUT_TTL);
        assert(bp.fresh&&bp.charge_w*bp.discharge_w==0);
        float grid=need-bp.discharge_w+bp.charge_w;
        final_ev=ev;final_charge=bp.charge_w;final_grid=grid;
        soc=fmaxf(0,fminf(100,soc+(bp.charge_w-bp.discharge_w)/720000.0f));
        float adjusted=pv_allocation_grid(&s,actual,grid,bp.charge_w,bp.discharge_w,soc,soc_ok&&power_ok);
        float available=battery_support_current(&buffer,&s,actual,adjusted,soc,soc_ok,bp.discharge_w,power_ok,
            p.cloud,p.assist,pv.target_a>0&&amps>=1,s.min_charge_a,now);
        float before=battery_solar_current(&s,actual,adjusted,soc,soc_ok,bp.discharge_w,power_ok);
        assert(isfinite(available)&&available>=0&&available<=16.001f);
        if(!soc_ok||!power_ok||soc<=s.battery_reserve_soc)assert(available<=before+.001f);
        bool assisted=p.assist&&soc_ok&&power_ok&&soc>s.battery_reserve_soc;
        pv_control_step_timed(&pv,meter_ok,pv_ramp_available_phases(available,pv.target_a,actual,p.phases),
            s.min_charge_a,16,assisted?s.min_charge_a:pv_start_threshold(&s),assisted?0:EMS_PV_START_MS,now);
        assert(isfinite(pv.target_a)&&pv.target_a>=0&&pv.target_a<=16);
        assert(pv.target_a==0||pv.target_a>=s.min_charge_a);
        if(!meter_ok)assert(pv.target_a==0);
        if(pv.target_a>0&&!running){starts++;if(first_start<0)first_start=t;}
        if(pv.target_a==0&&running){stops++;if(first_stop<0)first_stop=t;}
        running=pv.target_a>0;
        delayed_target[(t+7)%12]=pv.target_a;
        if(soc<=s.battery_reserve_soc)assert(!buffer.active&&!buffer.assist_active);
        assert(buffer.remaining_ms<=EMS_BATTERY_BUFFER_MS);
        peak_grid=fmaxf(peak_grid,grid);imported_wh+=fmaxf(0,grid)/3600;
        if(t%5==0)printf("DATA,%s,%u,%d,%.1f,%.1f,%.1f,%.1f,%.1f,%.1f,%.3f,%.3f,%.2f,%u,%d\n",p.name,p.phases,t,solar,house,grid,bp.charge_w,bp.discharge_w,ev,pv.target_a,available,soc,buffer.remaining_ms/1000,pv.phase);
    }
    printf("RESULT,%s,%u,%d,%d,%d,%d,%.1f,%.1f\n",p.name,p.phases,starts,stops,first_start,first_stop,peak_grid,imported_wh);
    if(p.kind==0||p.kind==2||p.kind==3||p.kind==5||p.kind==6||p.kind==7||(p.kind>=11&&p.kind<=15)||p.kind==18||p.kind==19||p.kind==22)assert(starts==1&&stops==0);
    if(p.kind==16)assert(starts==1&&stops==1);
    if(p.kind==17)assert(starts==2&&stops==1);
    if(p.kind==10)assert(starts==0&&stops==0); /* battery takes solar; insufficient remaining export */
    if(p.kind==1){assert(starts==1&&stops==1);assert(first_stop>=1020&&first_stop<=1060);}
    if(p.kind==4)assert(starts==2&&stops==1);
    if(p.kind==8)assert(starts==2&&stops==1&&first_stop==120);
    if(p.kind==20)assert(starts==1&&stops==1&&first_stop>=300&&first_stop<=331&&final_ev<1);
    if(p.kind==21)assert(starts==2&&stops==1&&first_stop>=120&&first_stop<=151);
    if(p.kind==22)assert(first_start>=330&&fabsf(final_ev-2400)<150&&final_charge<50);
    if(p.kind>=11&&p.kind<=14) {
        float expected=p.kind==11||p.kind==14?3680:p.kind==12?2900:3080;
        assert(fabsf(final_ev-expected)<150);
        assert(fabsf(final_charge-(p.kind==14?0:4400-expected))<150);
        if(p.kind==14)assert(final_grid<-600); /* full battery exports the remainder */
    }
}
int main(void){
    profile_t profiles[]={
        {"sunrise",0,1200,1,false,false,-1},
        {"cloud_15min",1,1320,1,true,false,1},
        {"short_clouds_1p",2,1800,1,true,false,1},
        {"short_clouds_3p",2,1800,3,true,false,1},
        {"heavy_load_10s",3,1800,3,true,true,1},
        {"heavy_load_5min",4,900,3,false,false,-1},
        {"reserve_with_solar",5,900,1,true,true,1},
        {"missing_soc",6,600,3,true,true,1},
        {"missing_power_10s",7,600,3,true,true,1},
        {"missing_meter_50s",8,600,3,true,true,1},
        {"battery_absorbs_solar",10,900,1,false,false,-1},
        {"battery_charges_car_first",11,900,1,false,false,1},
        {"battery_charges_house_first",12,900,1,false,false,0},
        {"battery_charges_shared",13,900,1,false,false,2},
        {"battery_full",14,900,1,false,false,0},
        {"charge_discharge_transition",15,900,1,true,true,1},
        {"battery_refuses_power",16,900,1,true,true,1},
        {"refused_battery_solar_returns",17,900,1,true,true,1},
        {"slow_ev_and_inverter",18,900,1,true,true,1},
        {"battery_charging_no_assist",19,900,1,true,false,1},
        {"reserve_without_solar",20,900,1,true,true,1},
        {"soc_loss_without_solar",21,600,1,true,true,1},
        {"battery_becomes_full",22,900,1,false,false,0},
    };
    for(unsigned i=0;i<sizeof(profiles)/sizeof(profiles[0]);i++)run(profiles[i]);
    for(unsigned phase=1;phase<=3;phase+=2)for(int priority=-1;priority<=2;priority++)
        run((profile_t){priority<0?"day_without_allocation":priority==0?"day_house_first":priority==1?"day_car_first":"day_shared",9,86400,phase,true,false,priority});
    puts("PASS: 31 closed-loop energy profiles, 8 complete days, 1/3 phases, clouds, large household loads, SOC limits, stale telemetry, reconnection, refused discharge, slow EV/inverter, battery charge/discharge and allocation; finite/bounded current and no artificial restart cycles");
}
