#include "ems_core.h"
#include "sungrow_modbus.h"
#include <assert.h>
#include <math.h>
#include <stdio.h>
#include <string.h>
static void word(uint8_t *p,uint16_t n){p[0]=n>>8;p[1]=n;}
static void dword(uint8_t *p,int32_t n){word(p,(uint16_t)n);word(p+2,(uint32_t)n>>16);}
int main(void){
 uint8_t b[46]={0},h[]={0x12,0x34,0,0,0,49,1,4,46},ex=0;sungrow_reading_t r={0};float w;
 assert(sungrow_reply_header(h,0x1234,1,23,&ex));
 h[1]++;assert(!sungrow_reply_header(h,0x1234,1,23,&ex));h[1]--;
 h[6]=2;assert(!sungrow_reply_header(h,0x1234,1,23,&ex));h[6]=1;
 h[5]=3;h[7]=0x84;h[8]=2;assert(!sungrow_reply_header(h,0x1234,1,23,&ex)&&ex==2);
 word(b,2);dword(b+18,2000);word(b+42,1500);word(b+44,731);
 assert(sungrow_sh_decode(b,46,&r)&&r.grid_ok&&r.battery_ok);
 assert(r.grid_w==-2000&&fabsf(r.soc-73.1f)<.001f&&r.charge_w==1500&&r.discharge_w==0);
 word(b,4);dword(b+18,-2600);assert(sungrow_sh_decode(b,46,&r)&&r.grid_w==2600&&r.discharge_w==1500);
 word(b,6);sungrow_sh_decode(b,46,&r);assert(!r.battery_ok&&r.grid_ok);
 word(b,0);word(b+42,0);sungrow_sh_decode(b,46,&r);assert(r.battery_ok&&r.charge_w==0&&r.discharge_w==0);
 word(b+44,1001);sungrow_sh_decode(b,46,&r);assert(!r.battery_ok&&r.grid_ok);
 assert(!sungrow_sh_decode(b,45,&r));
 dword(b,123456);assert(sungrow_power_decode(b,4,false,1,&w)&&w==123456);
 dword(b,-2000);assert(sungrow_power_decode(b,4,true,1,&w)&&w==-2000);
 dword(b,INT32_MAX);assert(!sungrow_power_decode(b,4,true,1,&w));
 memset(b,0,sizeof(b));dword(b,600);dword(b+4,-110);dword(b+12,-250);word(b+16,640);
 assert(sungrow_ihm_decode(b,18,&r)&&r.pv_ok&&r.grid_ok&&r.battery_ok);
 assert(r.pv_w==6000&&r.grid_w==-1100&&r.soc==64&&r.charge_w==2500);
 settings_t s,old,live;settings_defaults(&s);assert(!s.sungrow_enabled&&s.sungrow_port==502&&s.sungrow_unit_id==1);
 s.basic_mode=false;s.sungrow_enabled=s.sungrow_pv=s.sungrow_battery=true;strcpy(s.sungrow_host,"inverter.local");
 strcpy(s.house_meter_type,"sungrow");s.zero_feed_enabled=s.battery_protect=s.pv_display_enabled=true;
 s.mqtt_enabled=s.homeassistant_enabled=false;assert(settings_valid(&s));
 s.grid_guard_enabled=true;assert(!settings_valid(&s));s.grid_guard_enabled=false;
 old=live=s;s.sungrow_port=503;assert(!settings_apply_live(&live,&old,&s));s=old;
 s.sungrow_profile=1;strcpy(s.sungrow_model,"SG10RT");assert(!settings_valid(&s));
 s.sungrow_battery=false;strcpy(s.house_meter_type,"tasmota");s.zero_feed_enabled=s.battery_protect=false;assert(settings_valid(&s));
 s.sungrow_profile=2;strcpy(s.sungrow_model,"SG10KTL-MT");strcpy(s.house_meter_type,"sungrow");s.zero_feed_enabled=true;assert(settings_valid(&s));
 s.sungrow_port=0;assert(!settings_valid(&s));s.sungrow_port=502;
 strcpy(s.sungrow_host,"http://host");assert(!settings_valid(&s));strcpy(s.sungrow_host,"192.0.2.1");
 s.basic_mode=true;settings_basic_mode(&s);assert(!s.sungrow_enabled&&!s.sungrow_pv&&!s.sungrow_battery);
 puts("PASS: Sungrow endian/sign/scale, independent invalid values, profile limits and settings");
}
