#include "ems_core.h"
#include <assert.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
typedef int nvs_handle_t;
typedef int esp_err_t;
#define ESP_OK 0
#define ESP_FAIL -1
#define ESP_ERR_NVS_NOT_FOUND 2
#define NVS_READONLY 0
#define NVS_READWRITE 1
static settings_t settings;
static bool config_storage_ok=true,control_storage_ok=true,control_memory_pending,battery_use,battery_start_use;
static control_memory_t stored_control_memory,flash_memory;
static battery_buffer_t battery_buffer;
static pv_control_t pv_control;
static int locks,writes,commits,write_error;
static int64_t uptime,utc;
#define LOCK() assert(locks++==0)
#define UNLOCK() assert(--locks==0)
static int64_t now_ms(void){return uptime;}
static uint32_t calendar(int64_t *epoch,int64_t *midnight){*epoch=utc;*midnight=0;return utc>0?20261011:0;}
static esp_err_t nvs_open(const char *name,int mode,nvs_handle_t *h){(void)mode;assert(!locks&&!strcmp(name,"wallbox_ems"));*h=1;return ESP_OK;}
static esp_err_t nvs_get_blob(nvs_handle_t h,const char *key,void *out,size_t *size){(void)h;assert(!strcmp(key,"control_memory")&&*size==sizeof(flash_memory));memcpy(out,&flash_memory,sizeof(flash_memory));return ESP_OK;}
static esp_err_t nvs_set_blob(nvs_handle_t h,const char *key,const void *data,size_t size){(void)h;assert(!locks&&!strcmp(key,"control_memory")&&size==sizeof(flash_memory));writes++;if(write_error)return ESP_FAIL;memcpy(&flash_memory,data,size);return ESP_OK;}
static esp_err_t nvs_commit(nvs_handle_t h){(void)h;assert(!locks);commits++;return ESP_OK;}
static void nvs_close(nvs_handle_t h){(void)h;}
#include "control_storage.inc"
static const int64_t saved_utc=1791720000000LL;
static void fixture(void){
    settings_defaults(&settings);settings.battery_protect=settings.zero_feed_enabled=true;
    memset(&stored_control_memory,0,sizeof(stored_control_memory));memset(&battery_buffer,0,sizeof(battery_buffer));memset(&pv_control,0,sizeof(pv_control));
    control_storage_ok=true;control_memory_pending=battery_use=battery_start_use=false;uptime=1000;utc=0;writes=commits=write_error=0;
    battery_buffer_t b={.started=true,.started_at=1000};pv_control_t pv={.initialized=true,.stopped=true,.stopped_at=100000,.last_at=101000};
    control_memory_capture(&flash_memory,&b,&pv,true,true,101000,saved_utc);
    load_control_memory();assert(control_memory_pending&&battery_use&&battery_start_use&&!settings.enabled);
}
int main(void){
    fixture();assert(save_control_memory(false));assert(control_memory_pending&&writes==0&&!battery_buffer.started);
    uptime=5000;assert(save_control_memory(false));assert(control_memory_pending&&writes==0);
    utc=saved_utc+60000;uptime=6000;assert(save_control_memory(false));
    assert(!control_memory_pending&&battery_buffer.remaining_ms==740000&&pv_control.wait_ms==59000&&pv_control.target_a==0&&commits==0);
    fixture();uptime=19999;assert(save_control_memory(false)&&control_memory_pending);
    uptime=20000;assert(save_control_memory(false)&&!control_memory_pending);assert(battery_buffer.started&&battery_buffer.remaining_ms==0);
    fixture();LOCK();restore_control_memory_locked(uptime,0,true);UNLOCK();
    assert(!control_memory_pending&&battery_buffer.remaining_ms==0&&pv_control.target_a==0);
    /* A later writer cannot restore over timers started by an explicit command. */
    battery_buffer.started=true;battery_buffer.started_at=uptime;pv_control.target_a=10;
    uptime=2000;assert(save_control_memory(true));assert(battery_buffer.started_at==1000&&pv_control.target_a==10&&flash_memory.cloud_ms==899000);
    fixture();flash_memory.checksum^=1;control_memory_pending=false;load_control_memory();
    assert(!control_storage_ok&&!stored_control_memory.version&&!control_memory_pending);
    assert(save_control_memory(false)&&control_storage_ok&&control_memory_valid(&flash_memory,sizeof(flash_memory)));
    fixture();utc=saved_utc+60000;assert(save_control_memory(false));battery_start_use=false;write_error=1;
    assert(!save_control_memory(false)&&!control_storage_ok);write_error=0;
    assert(save_control_memory(false)&&control_storage_ok&&!(flash_memory.flags&2));
    assert(!locks);puts("PASS: NVS clock wait, preserved deadlines, explicit commands, bounded clock fallback, corrupt-blob recovery and retry after write failure");
}
