#include "ems_core.h"
#include "sungrow_modbus.h"
#include <assert.h>
#include <stdio.h>
#include <string.h>
static settings_t settings;
static bool wifi_online=true,restarting,ota_in_progress;
static int64_t house_current_at,house_power_at,actual_at,pv_at,pv_generation_at,battery_soc_at,battery_charge_at,battery_discharge_at;
static float house_power_w,pv_generation_w,battery_soc,battery_charge_w,battery_discharge_w;
static unsigned locked,requests,fail_register,bad_header,change_source,closed,shut;
static uint16_t reg,count,tx;static uint8_t unit;static int64_t clock_ms=10000;
#define SHUT_RDWR 2
#define LOCK() assert(locked++==0)
#define UNLOCK() assert(--locked==0)
static int64_t now_ms(void){return clock_ms;}
static unsigned ticks(unsigned ms){return ms;}
static void vTaskDelay(unsigned ms){assert(!locked);assert(ms==40);clock_ms+=ms;}
static uint32_t esp_random(void){return 0x1234;}
static int64_t wallbox_meter_ttl(void){return 5000;}
static float pv_available_current_locked(int64_t now,float w){assert(locked);(void)now;return -w/690;}
static void word(uint8_t *p,uint16_t n){p[0]=n>>8;p[1]=n;}
static void dword(uint8_t *p,int32_t n){word(p,n);word(p+2,(uint32_t)n>>16);}
static int tcp_connect_timeout(const char *host,int port,int timeout){assert(!locked&&host[0]&&port==502&&timeout==800);return 7;}
static int close(int fd){assert(fd==7&&!locked);closed++;return 0;}
static int shutdown(int fd,int how){assert(fd==7&&how==2&&!locked);shut++;return 0;}
static bool socket_exact(int fd,uint8_t *p,size_t n,bool write,int64_t deadline){
 assert(!locked&&fd==7&&deadline>now_ms());
 if(write){assert(n==12&&p[7]==4);tx=(uint16_t)(p[0]*256+p[1]);unit=p[6];reg=(uint16_t)(p[8]*256+p[9]);count=(uint16_t)(p[10]*256+p[11]);requests++;return !shut;}
 memset(p,0,n);
 if(n==9){word(p,tx+(bad_header?1:0));word(p+4,count*2+3);p[6]=unit;p[7]=4;p[8]=count*2;
  if(fail_register==reg){word(p+4,3);p[7]=0x84;p[8]=2;}return true;}
 if(reg==5016||reg==5030)dword(p,6000);
 else if(reg==13000){assert(n==46);word(p,2);dword(p+18,2000);word(p+42,500);word(p+44,730);}
 else if(reg==5082)dword(p,-2000);
 else if(reg==8154){assert(n==18&&unit==247);dword(p,600);dword(p+4,-200);dword(p+12,-50);word(p+16,730);}
 else assert(0);
 if(change_source)strcpy(settings.sungrow_host,"changed.local");return true;
}
#include "sungrow_network.inc"
static void run(unsigned profile){
 settings_defaults(&settings);settings.basic_mode=false;settings.sungrow_enabled=settings.sungrow_pv=true;
 settings.sungrow_battery=profile==0||profile==3;settings.sungrow_profile=profile;
 settings.sungrow_unit_id=profile==3?247:1;strcpy(settings.sungrow_host,"sungrow.local");
 strcpy(settings.house_meter_type,profile==1?"tasmota":"sungrow");settings.zero_feed_enabled=settings.pv_display_enabled=settings.battery_protect=true;
 house_current_at=55;actual_at=clock_ms;house_power_at=pv_at=pv_generation_at=battery_soc_at=sungrow_at=0;requests=closed=shut=0;
 unsigned failures=0;assert(sungrow_poll(&failures)>=5000);assert(!locked&&closed==1);
 (void)sungrow_task;
}
int main(void){
 for(unsigned p=0;p<4;p++){run(p);assert(pv_generation_at&&pv_generation_w==6000&&sungrow_at);
  if(p!=1)assert(house_power_at&&house_power_w==-2000&&house_current_at==0&&pv_at);
  if(p==0||p==3)assert(battery_soc_at&&battery_soc==73&&battery_charge_w==500&&battery_discharge_w==0);else assert(!battery_soc_at);
 }
 fail_register=13000;run(0);assert(pv_generation_at&&!battery_soc_at&&!house_power_at);fail_register=0;
 change_source=1;run(0);assert(!sungrow_at&&!pv_generation_at&&!house_power_at&&!battery_soc_at);change_source=0;
 bad_header=1;run(0);assert(shut&&!sungrow_at&&!pv_generation_at&&!house_power_at);
 puts("PASS: actual Sungrow worker, FC04 addresses, independent samples, no IO under lock, bad frames and changed source discarded");
}
