#include <assert.h>
#include <math.h>
#include <stdio.h>
#include <stdint.h>
#include <string.h>
#include <time.h>
#include "cJSON.h"
#include "ems_core.h"
static settings_t settings;
static bool mqtt_online=true,config_storage_ok;
static int64_t tick=100000;
static int64_t now_ms(void){return tick;}
typedef int nvs_handle_t;
#define NVS_READONLY 0
#define ESP_OK 0
static int nvs_open(const char *n,int m,nvs_handle_t *h){(void)n;(void)m;(void)h;return -1;}
static int nvs_get_u32(nvs_handle_t h,const char *k,uint32_t *f){(void)h;(void)k;(void)f;return -1;}
static void nvs_close(nvs_handle_t h){(void)h;}
static bool selected_meter_voltage_locked(int64_t now){(void)now;return false;}
#include "../main/personal_runtime.inc"
int main(void){
 settings_t s;settings_defaults(&s);s.charge_phases=3;s.power_factor=1;
 float a[3]={8,10,12},v[3]={234.7f,237.1f,232.5f};
 assert(!ems_phase_voltage_active());assert(fabsf(estimated_charge_power(&s,a)-6900)<.01f);
 ems_phase_voltage_override(v,true);assert(ems_phase_voltage_active());
 float expected=8*v[0]+10*v[1]+12*v[2];assert(fabsf(estimated_charge_power(&s,a)-expected)<.01f);
 assert(fabsf(ems_phase_voltage_sum(&s,3)-704.3f)<.01f);
 assert(fabsf(solar_current(&s,a,0)-expected/704.3f)<.001f);
 s.charge_phases=1;assert(fabsf(estimated_charge_power(&s,a)-8*v[0])<.01f);
 s.power_factor=.95f;assert(fabsf(estimated_charge_power(&s,a)-8*v[0]*.95f)<.01f);
 float idle[3]={.17f,.17f,.17f};assert(estimated_charge_power(&s,idle)==0);
 v[1]=NAN;ems_phase_voltage_override(v,true);assert(!ems_phase_voltage_active());
 assert(fabsf(estimated_charge_power(&s,a)-8*230*.95f)<.01f);
 v[1]=237.1f;ems_phase_voltage_override(v,true);ems_phase_voltage_override(NULL,false);assert(!ems_phase_voltage_active());
 settings_defaults(&settings);strcpy(settings.wallbox_meter_type,"xemex");settings.mqtt_enabled=true;
 personal_load();assert(!personal_voltage_enabled&&!personal_solis_enabled);
 char body[160];snprintf(body,sizeof(body),"{\"valid\":true,\"ts\":%lld,\"v\":[234.7,237.1,232.5]}",(long long)time(NULL));
 assert(!personal_voltage_message_locked(body));assert(!ems_phase_voltage_active());
 personal_voltage_enabled=true;assert(personal_voltage_message_locked(body));assert(ems_phase_voltage_active());
 tick+=31000;personal_sync_locked(tick);assert(!ems_phase_voltage_active());
 assert(personal_voltage_message_locked(body));assert(ems_phase_voltage_active());
 assert(personal_voltage_message_locked("{\"valid\":false}"));assert(!ems_phase_voltage_active());
 snprintf(body,sizeof(body),"{\"valid\":true,\"ts\":%lld,\"v\":[234.7,237.1,232.5]}",(long long)time(NULL)-31);
 assert(!personal_voltage_message_locked(body));assert(!ems_phase_voltage_active());
 puts("PASS: per-phase U*I, unequal loads, one phase, power factor, idle floor, invalid/disabled fallback.");
}
