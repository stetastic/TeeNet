#include "ems_core.h"
#include "shelly_modbus.h"
#include "em24_modbus.h"
#include <assert.h>
#include <math.h>
#include <stdio.h>
#include <string.h>
#define LOCK() ((void)0)
#define UNLOCK() ((void)0)
static settings_t settings;
static bool wifi_online=true,read_ok=true;
static shelly_modbus_reading_t sample={.current_a={9,3,4},.active_power_w=3680,.phases=3};
static bool read_em24_modbus(const char *host,uint8_t unit,em24_reading_t *out,int timeout){
    assert(host[0]&&unit==1&&timeout==2500);memcpy(out->current_a,sample.current_a,sizeof(out->current_a));out->active_power_w=sample.active_power_w;return read_ok;
}
static bool read_shelly_modbus(const char *host,const char *type,shelly_modbus_reading_t *out,int timeout){
    assert(host[0]&&type[0]&&timeout==2500);*out=sample;return read_ok;
}
#include "wallbox_network.inc"
int main(void){
    settings_defaults(&settings);strcpy(settings.wallbox_meter_host,"192.168.1.34");settings.charge_phases=1;
    const char *three[]={"shelly_gen2","em24_tcp"};float actual[3],watts;
    for(unsigned p=0;p<2;p++){
        strcpy(settings.wallbox_meter_type,three[p]);assert(read_wallbox_network(actual,&watts));
        assert(actual[0]==9&&actual[1]==3&&actual[2]==4&&watts==3680);
        assert(wallbox_inactive_phase_current(&settings,actual));
        float report[3];control_report(&settings,actual,9,0,false,report);assert(report[1]==0&&report[2]==0);
        settings.charge_phases=3;assert(!wallbox_inactive_phase_current(&settings,actual));settings.charge_phases=1;
    }
    const float idle_other[]={9,.02f,.04f};assert(!wallbox_inactive_phase_current(&settings,idle_other));
    strcpy(settings.wallbox_meter_type,"xemex");settings.xemex_coils=1;assert(!wallbox_inactive_phase_current(&settings,actual));
    settings.xemex_coils=3;assert(wallbox_inactive_phase_current(&settings,actual));
    strcpy(settings.wallbox_meter_type,"sdm630");assert(wallbox_inactive_phase_current(&settings,actual));
    strcpy(settings.wallbox_meter_type,"sdm230");assert(!wallbox_inactive_phase_current(&settings,actual));
    strcpy(settings.wallbox_meter_type,"shelly_em1");sample.phases=1;sample.active_power_w=2070;
    assert(read_wallbox_network(actual,&watts)&&actual[0]==9&&actual[1]==0&&actual[2]==0&&watts==2070);
    settings.charge_phases=3;assert(read_wallbox_network(actual,&watts)&&actual[1]==9&&actual[2]==9&&watts==6210);
    read_ok=false;assert(!read_wallbox_network(actual,&watts));read_ok=true;wifi_online=false;assert(!read_wallbox_network(actual,&watts));
    puts("PASS: raw Shelly/EM24 phases preserved, unexpected L2/L3 detected, reconstructed phases excluded, Shell L2/L3 remain zero, offline readings rejected");
}
