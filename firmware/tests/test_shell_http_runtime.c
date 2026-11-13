#include "shell_http_runtime.h"
#include <assert.h>
#include <stdio.h>

static void lifecycle(void){
    shell_http_runtime_t r={0};shell_http_sample_t sample={.pilot_valid=true};
    assert(shell_http_runtime_tick(&r,true,false,.8f,&sample,1000,1000,true));
    assert(!r.running&&!r.control.armed);
    /* No browser or heartbeat for more than a full day. Fresh device polling
       keeps the selected controller ready; the private test lease is unrelated. */
    for(int64_t now=1000;now<90000000;now+=1000){
        assert(shell_http_runtime_tick(&r,true,true,.8f,&sample,now,now,true));
        assert(shell_http_runtime_ready(&r,&sample,now,now,true));
    }
    assert(!shell_http_runtime_tick(&r,true,true,.8f,&sample,90000000,90004000,true));
    assert(r.control.fault);
    assert(!shell_http_runtime_tick(&r,true,true,.8f,&sample,90005000,90005000,true));
    assert(shell_http_runtime_tick(&r,true,false,.8f,&sample,90006000,90006000,true));
    assert(!r.running&&r.control.fault); /* Fresh data cannot resume Stop. */
    assert(shell_http_runtime_tick(&r,true,true,.8f,&sample,90007000,90007000,true));
    assert(r.running&&!r.control.fault); /* New explicit Start. */
    assert(!shell_http_runtime_tick(&r,true,true,.8f,&sample,90008000,90008000,false));
    assert(shell_http_runtime_tick(&r,false,false,.8f,&sample,0,90009000,false));
    assert(!r.running&&!r.control.armed&&!r.control.fault);
}
static void estimation_and_migration(void){
    float out[3];shell_http_sample_t sample={.pilot_valid=true,.pilot_a=8.25f,.car_charging=true};
    shell_http_runtime_currents(true,false,&sample,1000,1000,1,out);
    assert(out[0]==8&&out[1]==0&&out[2]==0);
    shell_http_runtime_currents(true,false,&sample,1000,1000,3,out);assert(out[0]==8&&out[1]==8&&out[2]==8);
    shell_http_runtime_currents(false,false,&sample,1000,1000,3,out);assert(!out[0]&&!out[1]&&!out[2]);
    shell_http_runtime_currents(true,false,&sample,1000,5000,3,out);assert(!out[0]&&!out[1]&&!out[2]);
    settings_t s,decoded;settings_defaults(&s);assert(!s.shell_http_control_enabled);
    s.shell_http_control_enabled=true;assert(!settings_valid(&s));
    strcpy(s.shell_setup_host,"192.168.1.65");strcpy(s.wallbox_meter_type,"shelly_gen2");s.wallbox_meter_host[0]=0;
    assert(settings_valid(&s)&&settings_decode(&s,sizeof(s),&decoded));
    assert(decoded.shell_http_control_enabled&&!decoded.enabled);
    settings_t before=s;before.shell_http_control_enabled=false;
    assert(!settings_apply_live(&before,&before,&s)); /* Controller owner changes require restart. */
    strcpy(s.wallbox_meter_host,"192.168.1.173");s.version=37;size_t end=offsetof(settings_t,shell_http_control_enabled),length=(end+3)&~(size_t)3;
    memset((char *)&s+end,0xa5,length-end);
    assert(settings_decode(&s,length,&decoded)&&!decoded.shell_http_control_enabled);
}
static void uart(shell_http_runtime_t *r,manual_report_filter_t *filter,settings_t settings,
    shell_http_sample_t sample,int64_t at,int64_t report_now,float request,bool current,float currents[3]){
    shell_http_runtime_t http_regulator=*r;manual_report_filter_t manual_filter=*filter;
    struct {shell_http_sample_t http_control;} evcc_shell={sample};int64_t evcc_shell_at=at;
    float actual_a[3]={17,18,19},house_power_w=0;bool house_ok=false;
    uint8_t req[8]={settings.wallbox_address,current?3:4,0x50,0x0c,0,6};
#include "shell_http_uart_actual.inc"
    if(http_reply_current)shell_http_trial_transmitted(&http_regulator.control,report_now,http_reply_trigger,
        report_now+5,request,settings.charge_phases,settings.grid_limit_a+http_regulator.control.normal_offset_a,
        settings.grid_limit_a+settings.max_charge_a+1,currents);
    *r=http_regulator;*filter=manual_filter;
}
static void production_uart(void){
    for(unsigned phases=1;phases<=3;phases+=2){
        settings_t s;settings_defaults(&s);s.shell_http_control_enabled=true;s.enabled=true;s.charge_phases=phases;
        shell_http_runtime_t manual={0},pv={0};manual_report_filter_t mf={0},pf={0};float a[3],b[3];
        shell_http_sample_t sample={.pilot_valid=true};
        assert(shell_http_runtime_tick(&manual,true,true,.8f,&sample,1000,1000,true));pv=manual;
        for(int64_t now=1000;now<=60000;now+=2000){
            if(now>=20000){sample.car_charging=true;sample.pilot_a=now<28000?9.4f:8.05f;}
            s.mode=MODE_MANUAL;uart(&manual,&mf,s,sample,now,now,7.8f,true,a);
            s.mode=MODE_PV;uart(&pv,&pf,s,sample,now,now,7.8f,true,b);
            assert(!manual.control.fault&&!pv.control.fault&&!memcmp(a,b,sizeof(a)));
            shell_http_runtime_t snapshot=pv;manual_report_filter_t saved=pf;
            uart(&pv,&pf,s,sample,now,now,7.8f,false,b);
            assert(!memcmp(&snapshot,&pv,sizeof(pv))&&!memcmp(&saved,&pf,sizeof(pf)));
        }
        uart(&pv,&pf,s,sample,60000,64000,7.8f,true,b);
        assert(pv.control.fault&&b[0]==49&&!pf.active);
        if(phases==1)assert(b[1]==0&&b[2]==0);else assert(b[1]==49&&b[2]==49);
    }
}
int main(void){lifecycle();estimation_and_migration();production_uart();puts("PASS: autonomous HTTP runtime, persistent opt-in, migration, no heartbeat dependency, fault latch, PV/manual UART composition and pure reads");}
