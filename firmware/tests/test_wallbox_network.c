#include "ems_core.h"
#include "shelly_modbus.h"
#include "em24_modbus.h"
#include "wallbox_modbus_session.h"
#include <assert.h>
#include <math.h>
#include <stdio.h>
#include <string.h>
#define LOCK() ((void)0)
#define UNLOCK() ((void)0)
static settings_t settings;
static uint32_t wallbox_meter_revision;
static wallbox_modbus_session_t session={.sock=-1};
static unsigned session_closes;
static bool wifi_online=true,read_ok=true;
static shelly_modbus_reading_t sample={.current_a={9,3,4},.active_power_w=3680,.phases=3};
static bool read_em24_modbus(const char *host,uint8_t unit,em24_reading_t *out,int timeout){
    assert(host[0]&&unit==1&&timeout==2500);memcpy(out->current_a,sample.current_a,sizeof(out->current_a));out->active_power_w=sample.active_power_w;return read_ok;
}
static bool read_shelly_modbus(const char *host,const char *type,shelly_modbus_reading_t *out,int timeout){
    assert(host[0]&&type[0]&&timeout==2500);*out=sample;return read_ok;
}
static inline void wallbox_modbus_session_close(wallbox_modbus_session_t *s){if(s->sock>=0)session_closes++;s->sock=-1;}
static void encode_float(uint8_t *data,unsigned reg,float value){
    uint32_t bits;memcpy(&bits,&value,4);unsigned offset=(reg-SHELLY_EM_START)*2;
    data[offset]=bits>>8;data[offset+1]=bits;data[offset+2]=bits>>24;data[offset+3]=bits>>16;
}
static inline bool wallbox_modbus_session_read(wallbox_modbus_session_t *s,const char *host,uint8_t unit,uint32_t revision,uint16_t start,uint16_t count,uint8_t *data,size_t capacity,int timeout){
    assert(s==&session&&host[0]&&unit==1&&revision==wallbox_meter_revision&&start==SHELLY_EM_START&&count==SHELLY_EM_COUNT&&capacity==count*2&&timeout==2500);
    if(!read_ok){wallbox_modbus_session_close(s);return false;}s->sock=42;memset(data,0,capacity);
    encode_float(data,1013,sample.active_power_w);
    for(unsigned i=0;i<3;i++){encode_float(data,1022+20*i,sample.current_a[i]);encode_float(data,1020+20*i,sample.voltage_valid?sample.voltage_v[i]:NAN);}
    return true;
}
#include "wallbox_network.inc"
int main(void){
    settings_defaults(&settings);strcpy(settings.wallbox_meter_host,"192.168.1.34");settings.charge_phases=1;settings.wallbox_shelly_reverse=false;
    const char *three[]={"shelly_gen2","em24_tcp"};float actual[3],voltage[3],watts;bool voltage_valid;
    for(unsigned p=0;p<2;p++){
        strcpy(settings.wallbox_meter_type,three[p]);assert(read_wallbox_network(&session,actual,&watts,voltage,&voltage_valid));
        assert(actual[0]==9&&actual[1]==3&&actual[2]==4&&watts==3680);
        assert(wallbox_inactive_phase_current(&settings,actual));
        float report[3];control_report(&settings,actual,9,0,false,report);assert(report[1]==0&&report[2]==0);
        settings.charge_phases=3;assert(!wallbox_inactive_phase_current(&settings,actual));settings.charge_phases=1;
    }
    const float idle_other[]={9,.02f,.04f};assert(!wallbox_inactive_phase_current(&settings,idle_other));
    strcpy(settings.wallbox_meter_type,"shelly_gen2");sample.voltage_valid=true;
    sample.voltage_v[0]=234.7f;sample.voltage_v[1]=235.6f;sample.voltage_v[2]=236.6f;
    assert(read_wallbox_network(&session,actual,&watts,voltage,&voltage_valid)&&voltage_valid&&voltage[0]==234.7f);
    settings.wallbox_shelly_reverse=true; /* Order is independent of feedback/contactor activation. */
    assert(read_wallbox_network(&session,actual,&watts,voltage,&voltage_valid)&&voltage_valid&&voltage[0]==236.6f&&actual[0]==4);
    settings.wallbox_shelly_reverse=false;sample.voltage_valid=false;
    assert(read_wallbox_network(&session,actual,&watts,voltage,&voltage_valid)&&!voltage_valid&&session.sock<0);
    sample.voltage_valid=true;sample.current_a[0]=NAN;
    unsigned closed=session_closes;assert(!read_wallbox_network(&session,actual,&watts,voltage,&voltage_valid)&&session.sock<0&&session_closes==closed+1);
    sample.current_a[0]=9;sample.active_power_w=NAN;closed=session_closes;
    assert(!read_wallbox_network(&session,actual,&watts,voltage,&voltage_valid)&&session.sock<0&&session_closes==closed+1);
    sample.active_power_w=3680;
    strcpy(settings.wallbox_meter_type,"xemex");settings.xemex_coils=1;assert(!wallbox_inactive_phase_current(&settings,actual));
    settings.xemex_coils=3;assert(wallbox_inactive_phase_current(&settings,actual));
    strcpy(settings.wallbox_meter_type,"sdm630");assert(wallbox_inactive_phase_current(&settings,actual));
    strcpy(settings.wallbox_meter_type,"sdm230");assert(!wallbox_inactive_phase_current(&settings,actual));
    strcpy(settings.wallbox_meter_type,"shelly_em1");sample.phases=1;sample.active_power_w=2070;
    assert(read_wallbox_network(&session,actual,&watts,voltage,&voltage_valid)&&actual[0]==9&&actual[1]==0&&actual[2]==0&&watts==2070);
    settings.charge_phases=3;assert(read_wallbox_network(&session,actual,&watts,voltage,&voltage_valid)&&actual[1]==9&&actual[2]==9&&watts==6210);
    read_ok=false;assert(!read_wallbox_network(&session,actual,&watts,voltage,&voltage_valid));read_ok=true;wifi_online=false;session.sock=42;closed=session_closes;
    assert(!read_wallbox_network(&session,actual,&watts,voltage,&voltage_valid)&&session.sock<0&&session_closes==closed+1);
    puts("PASS: raw Shelly/EM24 phases preserved, unexpected L2/L3 detected, reconstructed phases excluded, Shell L2/L3 remain zero, offline readings rejected");
}
