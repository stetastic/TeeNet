#include "ems_core.h"
#include <assert.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
typedef int nvs_handle_t;
typedef int esp_err_t;
enum{ESP_OK=0,ESP_FAIL=-1,ESP_ERR_NVS_NOT_FOUND=1,NVS_READONLY=0,NVS_READWRITE=1};
#define TAG "test"
#define ESP_LOGW(...) ((void)0)
static bool config_storage_ok=true,boot_config_ok=true,fail_save;
static uint32_t loaded_settings_version;
static settings_t settings,saved_settings;
static unsigned char legacy[sizeof(settings_t)],previous[sizeof(settings_t)],current[sizeof(settings_t)];
static size_t legacy_size,previous_size,current_size;
static int nvs_open(const char *ns,int mode,nvs_handle_t *h){assert(!strcmp(ns,"wallbox_ems"));(void)mode;*h=1;return ESP_OK;}
static void nvs_close(nvs_handle_t h){assert(h==1);}
static int nvs_commit(nvs_handle_t h){assert(h==1);return ESP_OK;}
static int nvs_get_blob(nvs_handle_t h,const char *key,void *dest,size_t *size){
    assert(h==1);const void *data=NULL;size_t n=0;
    if(!strcmp(key,"settings_v21")){data=legacy;n=legacy_size;}
    if(!strcmp(key,"settings_v33")){data=previous;n=previous_size;}
    if(!strcmp(key,EMS_SETTINGS_STORAGE_KEY)){data=current;n=current_size;}
    if(!n)return ESP_ERR_NVS_NOT_FOUND;
    assert(*size>=n);memcpy(dest,data,n);*size=n;return ESP_OK;
}
static int nvs_set_blob(nvs_handle_t h,const char *key,const void *data,size_t size){
    assert(h==1&&!strcmp(key,EMS_SETTINGS_STORAGE_KEY)&&strcmp(key,"settings_v21"));
    if(fail_save)return ESP_FAIL;
    assert(size<=sizeof(current));memcpy(current,data,size);current_size=size;return ESP_OK;
}
#include "config_storage_actual.inc"
int main(void){
    assert(sizeof(EMS_SETTINGS_STORAGE_KEY)<=16);
    settings_t old;settings_defaults(&old);old.version=27;old.basic_mode=false;
    old.relay_active_low=true;old.phase_feedback_closed_is_single=true;
    strcpy(old.wifi_ssid,"Existing network");strcpy(old.wifi_password,"Existing password");
    legacy_size=(offsetof(settings_t,phase_actuator_type)+3)&~(size_t)3;memcpy(legacy,&old,legacy_size);
    unsigned char original[sizeof(legacy)];memcpy(original,legacy,legacy_size);
    load_settings();assert(boot_config_ok&&loaded_settings_version==27);
    assert(settings.relay_active_low&&settings.phase_feedback_closed_is_single);
    assert(!strcmp(settings.wifi_ssid,old.wifi_ssid));
    settings.battery_reserve_soc=55;assert(save_config(&settings)==ESP_OK);
    assert(!memcmp(original,legacy,legacy_size));
    load_settings();assert(loaded_settings_version==EMS_SETTINGS_VERSION&&settings.battery_reserve_soc==55);
    assert(settings.relay_active_low&&settings.phase_feedback_closed_is_single&&!settings.enabled);
    assert(!strcmp(settings.wifi_password,old.wifi_password));
    /* An older decoder still finds its original schema, not the newer struct. */
    uint32_t legacy_version;memcpy(&legacy_version,legacy,sizeof(legacy_version));assert(legacy_version==27);
    fail_save=true;assert(save_config(&settings)==ESP_FAIL&&!memcmp(original,legacy,legacy_size));
    /* A future loader also scans per-schema keys, not just the old shared key. */
    settings.version=33;settings.battery_reserve_soc=44;previous_size=(offsetof(settings_t,heatpump_enabled)+3)&~(size_t)3;
    memcpy(previous,&settings,previous_size);current_size=0;load_settings();
    assert(loaded_settings_version==33&&settings.battery_reserve_soc==44&&settings.relay_active_low);
    puts("PASS: actual NVS loader/save migrates legacy configuration without overwriting rollback data; new key wins, credentials/polarity preserved, failure leaves legacy intact");
}
