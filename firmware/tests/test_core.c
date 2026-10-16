#include "ems_core.h"
#include <assert.h>
#include <math.h>
#include <stdio.h>
#include <string.h>
static void near(double a,double b) { if(fabs(a-b)>1e-6) { fprintf(stderr,"Mismatch: %.12f != %.12f\n",a,b); fflush(stderr); assert(0); } }
static energy_t e;
static void step(int64_t t,double a,double b,int64_t expires,uint32_t day) {
    double power[]={a,b}; int64_t until[]={expires,expires};
    energy_step(&e,t,1704067200000LL+t,day,1704067200000LL,power,until);
}
int main(void) {
    settings_t s; settings_defaults(&s); assert(settings_valid(&s));
    s.wallbox_tx_pin=19; assert(!settings_valid(&s)); settings_defaults(&s);
    s.xemex_rx_pin=s.wallbox_tx_pin; assert(!settings_valid(&s)); settings_defaults(&s);
    s.power_factor=NAN; assert(!settings_valid(&s)); settings_defaults(&s);
    s.min_charge_a=17; assert(!settings_valid(&s)); settings_defaults(&s);
    strcpy(s.mqtt_uri,"mqtt://user:secret@host"); assert(!settings_valid(&s)); settings_defaults(&s);
    s.pv_display_enabled=true;s.mqtt_input_source=2; assert(!settings_valid(&s)); settings_defaults(&s);
    memset(s.house_meter_password,'x',sizeof(s.house_meter_password));assert(!settings_valid(&s));settings_defaults(&s);
    memset(s.wallbox_meter_password,'x',sizeof(s.wallbox_meter_password));assert(!settings_valid(&s));settings_defaults(&s);
    double number; assert(!parse_number("nan",0,63,&number)); assert(!parse_number("inf",0,63,&number));
    assert(!parse_number("12junk",0,63,&number)); assert(!parse_number("",0,63,&number));
    assert(!parse_number("-1",0,63,&number)); assert(parse_number(" 16.25 ",0,63,&number)); near(number,16.25);
    assert(!fresh(10000,0,30000)); assert(fresh(30001,1,30000)); assert(!fresh(30002,1,30000));
    s.enabled=true; s.mode=MODE_MANUAL; s.manual_current_a=16;
    near(control_target(&s,false,true,true),0);
    near(control_target(&s,true,true,true),16);
    near(control_target(&s,true,true,true),16); s.control_verified=true;
    near(control_target(&s,true,true,true),16); near(control_target(&s,false,true,true),0);
    near(control_target(&s,true,false,true),0); s.mode=MODE_PV; s.pv_surplus_a=16;
    near(control_target(&s,true,true,false),0); s.pv_surplus_a=7; near(control_target(&s,true,true,true),0);
    s.mode=MODE_MANUAL; s.min_charge_a=8; s.manual_current_a=8;
    s.current_offset_a=0; /* baseline without installation-specific calibration */
    float out[3];
    /* Shell receives grid current. Fresh total house power minus Wallbox current
       preserves other household use while requesting the configured current. */
    s.grid_limit_a=16;
    float charging[]={16,16,16},idle[]={0,0,0};
    control_report(&s,charging,8,3*230*16,true,out); near(out[0],24);
    control_report(&s,charging,10,3*230*16,true,out); near(out[0],22);
    control_report(&s,charging,12,3*230*16,true,out); near(out[0],20);
    control_report(&s,charging,8,3*230*26,true,out); near(out[0],24); /* house meter ignored outside PV */
    control_report(&s,idle,8,-3*230*4,true,out); near(out[0],8);
    control_report(&s,idle,0,0,true,out); near(out[0],33);
    control_report(&s,charging,8,NAN,false,out); near(out[0],24); /* safe fallback without house data */
    float missing[]={NAN,NAN,NAN}; control_report(&s,missing,8,NAN,false,out); near(out[0],33);
    /* Stop must remain asserted across the full charging-to-idle trajectory,
       on every phase and independent of house power or measurement failure. */
    s.grid_limit_a=32; s.min_charge_a=8;
    const float stop_inputs[]={0,1,3,4,6,7,7.99f};
    for(unsigned i=0;i<sizeof(stop_inputs)/sizeof(stop_inputs[0]);i++) {
        s.manual_current_a=stop_inputs[i]; near(control_target(&s,true,true,true),0);
        for(int current=16;current>=0;current--) {
            float measured[]={current,current,current};
            control_report(&s,measured,0,-5000,true,out);
            for(int p=0;p<3;p++) assert(out[p]-s.grid_limit_a>s.max_charge_a);
        }
    }
    s.manual_current_a=6; near(control_target(&s,true,true,true),0);
    s.manual_current_a=8; near(control_target(&s,true,true,true),8);
    settings_t migrated; s.min_charge_a=7.5f; assert(settings_valid(&s));
    assert(settings_decode(&s,sizeof(s),&migrated)); near(migrated.min_charge_a,7.5);
    near(migrated.grid_limit_a,32); assert(!migrated.enabled);
    s.min_charge_a=8; assert(settings_valid(&s));
    control_report(&s,charging,7,0,false,out); near(out[0],49);
    control_report(&s,charging,0,0,false,out); near(out[0],49);
    /* A confirmed one-phase position counts L1 once. */
    s.basic_mode=false;s.phase_switch_enabled=true; s.relay_board_enabled=true; s.relay1_mode=3; s.charge_phases=1; s.manual_phases=1;
    s.manual_current_a=6; s.current_offset_a=.8f;
    assert(settings_valid(&s));
    near(control_target(&s,true,true,true),0);s.manual_current_a=8;
    near(control_target(&s,true,true,true),8);
    float single[]={8.5f,8.5f,8.5f};
    near(estimated_charge_power(&s,single),8.5f*230);
    s.mode=MODE_PV;control_report(&s,single,8,0,false,out);
    near(out[0],33.3); near(out[1],0); near(out[2],0);
    assert(settings_decode(&s,sizeof(s),&migrated));
    assert(migrated.phase_switch_enabled && migrated.charge_phases==3 && migrated.manual_phases==3 && !migrated.enabled);
    s.phase_switch_enabled=false; s.charge_phases=3; s.manual_phases=3; s.current_offset_a=0;
    s.grid_limit_a=25;
    /* Known Modbus CRC vector, external to the implementation. */
    const uint8_t known[]={1,3,0,0,0,10,0xc5,0xcd}; assert(valid_frame(known,sizeof(known)));
    uint8_t req[8]={1,3,0x50,0x0c,0,6}; append_crc(req,6); uint8_t reply[256]; float values[]={1,16.5f,0};
    size_t len=modbus_reply(req,8,reply,&s,values); assert(len==17 && reply[3]==0x3f && reply[4]==0x80);
    assert(decode_meter(reply,len,1,out)); near(out[0],1); near(out[1],16.5); near(out[2],0);
    reply[4]^=1; assert(!decode_meter(reply,len,1,out));
    values[0]=NAN; len=modbus_reply(req,8,reply,&s,values); assert(!decode_meter(reply,len,1,out)); values[0]=1;
    req[2]=0xff;req[3]=0xff;req[5]=2;append_crc(req,6);len=modbus_reply(req,8,reply,&s,values);
    assert(len==5 && reply[1]==0x83 && reply[2]==2 && valid_frame(reply,len));
    req[1]=6;append_crc(req,6);len=modbus_reply(req,8,reply,&s,values);assert(len==5 && reply[2]==1);
    req[0]=0;append_crc(req,6);assert(modbus_reply(req,8,reply,&s,values)==0);
    /* 1 kW and 11 kW for exactly one hour. */
    memset(&e,0,sizeof(e));
    for(int64_t t=0;t<=3600000;t+=1000) step(t,1000,11000,t+30000,20240101);
    near(e.store.total_wh[0],1000); near(e.store.total_wh[1],11000); assert(energy_store_valid(&e.store));
    near(e.store.days[0].wh[1],11000); assert(e.store.days[0].covered_ms[0]==3600000);
    assert(e.store.days[0].charging_ms==3600000);
    typedef struct { uint32_t date; double wh[EMS_SOURCES]; uint64_t covered_ms[EMS_SOURCES]; } old_day_t;
    typedef struct { uint32_t version; double total_wh[EMS_SOURCES],undated_wh[EMS_SOURCES]; old_day_t days[EMS_DAYS]; } old_store_t;
    old_store_t old={.version=1};
    memcpy(old.total_wh,e.store.total_wh,sizeof(old.total_wh));
    memcpy(old.undated_wh,e.store.undated_wh,sizeof(old.undated_wh));
    for(int d=0;d<EMS_DAYS;d++) {
        old.days[d].date=e.store.days[d].date;
        memcpy(old.days[d].wh,e.store.days[d].wh,sizeof(old.days[d].wh));
        memcpy(old.days[d].covered_ms,e.store.days[d].covered_ms,sizeof(old.days[d].covered_ms));
    }
    energy_store_t upgraded={0};
    assert(energy_store_decode(&old,sizeof(old),&upgraded));
    near(upgraded.total_wh[0],1000); assert(upgraded.version==2);
    assert(upgraded.days[0].covered_ms[0]==3600000 && upgraded.days[0].charging_ms==0);
    assert(!energy_store_decode(&old,sizeof(old)-1,&upgraded));
    memset(&e,0,sizeof(e));
    step(0,500,500,10000,20240101); step(1000,500,500,10000,20240101);
    assert(e.store.days[0].covered_ms[0]==1000 && e.store.days[0].charging_ms==0);
    /* A sample expires at 2.5 s; do not fabricate the following 57.5 s. */
    memset(&e,0,sizeof(e));
    for(int64_t t=0;t<=60000;t+=1000) step(t,3600,7200,2500,20240101);
    near(e.store.total_wh[0],2.5); near(e.store.total_wh[1],5);
    step(61000,3600,7200,90000,20240101); near(e.store.total_wh[0],2.5);
    step(62000,3600,7200,90000,20240101); near(e.store.total_wh[0],3.5);
    /* No data, NaN, and scheduler stalls contribute no made-up energy. */
    memset(&e,0,sizeof(e)); step(0,NAN,0,10000,0); step(1000,3600,3600,10000,0); near(e.store.total_wh[0],0);
    step(2000,3600,3600,10000,0); near(e.store.undated_wh[0],1);
    step(100000,3600,3600,120000,0); near(e.store.total_wh[0],1);
    /* Split one sample at midnight, with a valid clock. */
    memset(&e,0,sizeof(e)); double power[]={3600,7200};int64_t until[]={10000,10000};
    energy_step(&e,1000,1704153599500LL,20240101,1704067200000LL,power,until);
    energy_step(&e,2000,1704153600500LL,20240102,1704153600000LL,power,until);
    near(e.store.days[0].wh[0],.5);near(e.store.days[1].wh[0],.5);
    /* Clock jumps retain total energy without assigning it to a false day. */
    energy_step(&e,3000,1704240000500LL,20240103,1704240000000LL,power,until);
    near(e.store.total_wh[0],2);near(e.store.undated_wh[0],1);
    /* Reboot keeps totals but never integrates the power-off interval. */
    energy_store_t persisted=e.store;memset(&e,0,sizeof(e));e.store=persisted;
    step(500,3600,3600,30000,20240103);near(e.store.total_wh[0],2);near(e.boot_wh[0],0);
    /* Ring buffers retain the latest 30 days / 120 minutes. */
    memset(&e,0,sizeof(e));
    for(int d=1;d<=31;d++) { step(d*2000,3600,3600,d*2000+10000,20240100+d);step(d*2000+1000,3600,3600,d*2000+10000,20240100+d); }
    bool oldest=false,newest=false;for(int d=0;d<EMS_DAYS;d++){oldest|=e.store.days[d].date==20240101;newest|=e.store.days[d].date==20240131;}assert(!oldest && newest);
    memset(&e,0,sizeof(e));for(int64_t t=0;t<=7300000;t+=1000) step(t,3600,0,t+30000,20240101);
    assert(e.history[0].minute==120);near(e.history[0].wh[0],60);
    puts("PASS: settings, numeric input, freshness, control interlocks, feedback, overload, Modbus CRC/registers/errors, energy integration, expiry, missing data, midnight, clock jump, reboot, ring buffers");
    return 0;
}
