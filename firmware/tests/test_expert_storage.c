#include "ems_core.h"
#include <assert.h>
#include <math.h>
#include <stdio.h>
#include <string.h>
typedef int esp_err_t;
typedef int nvs_handle_t;
#define ESP_OK 0
#define ESP_FAIL -1
#define ESP_ERR_TIMEOUT 1
#define ESP_ERR_INVALID_STATE 2
#define NVS_READWRITE 1
#define PHASE_READY 0
#define pdTRUE 1
#define ticks(x) (x)
static settings_t settings;
static expert_values_t expert_saved,flash_values;
static bool ota_in_progress,restarting,backup_in_progress,evcc_configuring,expert_reboot_pending,config_storage_ok=true;
static struct {bool configured,enabled;} evcc_control;
static int phase_state,maintenance_flash_lock,locks,flash_locked,writes,write_error;
static bool start_during_wait;
static int64_t actual_at=1000;
static float actual_a[3];
#define LOCK() assert(locks++==0)
#define UNLOCK() assert(--locks==0)
static int64_t now_ms(void){return 2000;}
static int64_t wallbox_meter_ttl(void){return 5000;}
static int xSemaphoreTake(int lock,int wait){(void)lock;(void)wait;assert(!locks);if(flash_locked)return 0;flash_locked=1;if(start_during_wait)settings.enabled=true;return pdTRUE;}
static void xSemaphoreGive(int lock){(void)lock;assert(!locks&&flash_locked);flash_locked=0;}
static esp_err_t nvs_open(const char *name,int mode,nvs_handle_t *h){(void)mode;assert(!locks&&flash_locked&&expert_reboot_pending&&!strcmp(name,"expert"));*h=1;return ESP_OK;}
static esp_err_t nvs_set_blob(nvs_handle_t h,const char *key,const void *v,size_t size){(void)h;assert(!locks&&flash_locked&&expert_reboot_pending&&!strcmp(key,"values")&&size==sizeof(flash_values));writes++;if(write_error)return ESP_FAIL;memcpy(&flash_values,v,size);return ESP_OK;}
static esp_err_t nvs_commit(nvs_handle_t h){(void)h;assert(!locks&&flash_locked&&expert_reboot_pending);return ESP_OK;}
static void nvs_close(nvs_handle_t h){(void)h;}
#include "expert_storage.inc"
int main(void){
    settings_defaults(&settings);expert_defaults(&expert_saved);expert_values_t next=expert_saved;
    next.values[EP_PV_START]=31;
    start_during_wait=true;assert(save_expert_values(&next)==ESP_ERR_INVALID_STATE&&writes==0&&!expert_reboot_pending&&!flash_locked);
    start_during_wait=false;settings.enabled=false;actual_a[0]=2;
    assert(save_expert_values(&next)==ESP_ERR_INVALID_STATE&&writes==0);actual_a[0]=0;
    evcc_control.configured=evcc_control.enabled=true;assert(save_expert_values(&next)==ESP_ERR_INVALID_STATE&&writes==0);evcc_control.enabled=false;
    actual_at=0;assert(save_expert_values(&next)==ESP_ERR_INVALID_STATE&&writes==0);actual_at=1000;
    write_error=1;assert(save_expert_values(&next)==ESP_FAIL&&!expert_reboot_pending&&expert_saved.values[EP_PV_START]==30);
    write_error=0;assert(save_expert_values(&next)==ESP_OK&&expert_reboot_pending&&expert_saved.values[EP_PV_START]==31&&ems_param(EP_PV_START)==30);
    unsigned before=writes;assert(save_expert_values(&next)==ESP_OK&&writes==(int)before&&expert_reboot_pending);
    next.values[EP_PV_START]=32;write_error=1;assert(save_expert_values(&next)==ESP_FAIL&&expert_reboot_pending&&expert_saved.values[EP_PV_START]==31);
    write_error=0;expert_defaults(&next);assert(save_expert_values(&next)==ESP_OK&&!expert_reboot_pending);
    flash_locked=1;assert(save_expert_values(&next)==ESP_ERR_TIMEOUT);flash_locked=0;
    assert(!locks);puts("PASS: atomic expert stop recheck, start lock during NVS, fresh idle measurements, evcc exclusion, write rollback and pending-state reset");
}
