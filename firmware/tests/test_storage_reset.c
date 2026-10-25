#include "ems_core.h"
#include "charge_sessions.h"
#include <assert.h>
#include <stdlib.h>
#include <stdio.h>
#include <string.h>

typedef int esp_err_t;
typedef int nvs_handle_t;
typedef struct { int status; } httpd_req_t;
#define ESP_OK 0
#define NVS_READWRITE 1
#define HTTPD_500_INTERNAL_SERVER_ERROR 500
#define pdTRUE 1
static int maintenance_flash_lock=1,locks,flash_held;
static unsigned takes,gives,writes,commits;
static bool stats_storage_ok,storage_error,clock_valid,fail_write;
static energy_t energy;
static sessions_t sessions;
static double energy_reset_wh[EMS_SOURCES];
static uint32_t energy_reset_date;
static int64_t last_saved_ms;
static session_store_t persisted_sessions;
static double persisted_reset[EMS_SOURCES];
static uint32_t persisted_date;
#define LOCK() assert(locks++==0)
#define UNLOCK() assert(--locks==0)
static int64_t now_ms(void){return 120000;}
static unsigned ticks(unsigned value){return value;}
static bool authorized(httpd_req_t *req){(void)req;return true;}
static uint32_t calendar(int64_t *epoch,int64_t *midnight){*epoch=1792742400000LL;*midnight=1792713600000LL;return clock_valid?20261023:0;}
static int xSemaphoreTake(int semaphore,unsigned wait){assert(semaphore==maintenance_flash_lock&&wait==2000&&!locks);takes++;if(flash_held)return 0;flash_held=1;return pdTRUE;}
static void xSemaphoreGive(int semaphore){assert(semaphore==maintenance_flash_lock&&flash_held&&!locks);flash_held=0;gives++;}
static int httpd_resp_set_status(httpd_req_t *req,const char *status){req->status=!strncmp(status,"409",3)?409:503;return ESP_OK;}
static int httpd_resp_sendstr(httpd_req_t *req,const char *text){(void)req;(void)text;return ESP_OK;}
static int httpd_resp_send_err(httpd_req_t *req,int status,const char *text){(void)text;req->status=status;return ESP_OK;}
static int ok_json(httpd_req_t *req){req->status=200;return ESP_OK;}
static int nvs_open_from_partition(const char *partition,const char *space,int mode,nvs_handle_t *handle){
    assert(!strcmp(partition,"stats")&&!strcmp(space,"energy")&&mode==NVS_READWRITE);
    assert(flash_held&&!locks);*handle=7;return ESP_OK;
}
static int nvs_set_blob(nvs_handle_t handle,const char *key,const void *value,size_t bytes){
    assert(handle==7&&flash_held&&!locks);writes++;if(fail_write)return -1;
    if(!strcmp(key,"sessions")){assert(bytes==sizeof(persisted_sessions));memcpy(&persisted_sessions,value,bytes);}
    else if(!strcmp(key,"reset_base")){assert(bytes==sizeof(persisted_reset));memcpy(persisted_reset,value,bytes);}
    else assert(!strcmp(key,"totals")&&bytes==sizeof(energy_store_t));
    return ESP_OK;
}
static int nvs_set_u32(nvs_handle_t handle,const char *key,uint32_t value){assert(handle==7&&flash_held&&!strcmp(key,"reset_date"));persisted_date=value;return ESP_OK;}
static int nvs_commit(nvs_handle_t handle){assert(handle==7&&flash_held&&!locks);commits++;return ESP_OK;}
static void nvs_close(nvs_handle_t handle){assert(handle==7&&flash_held&&!locks);}
#include "storage_reset_generated.inc"
static void fixture(void){
    memset(&energy,0,sizeof(energy));sessions_init(&sessions);sessions.store.count=1;sessions.store.records[0].id=42;
    persisted_sessions=sessions.store;energy.store.total_wh[0]=12000;energy.store.total_wh[1]=11000;
    energy_reset_wh[0]=100;energy_reset_wh[1]=90;energy_reset_date=persisted_date=20260901;
    memcpy(persisted_reset,energy_reset_wh,sizeof(persisted_reset));
    stats_storage_ok=clock_valid=true;storage_error=fail_write=false;locks=flash_held=0;takes=gives=writes=commits=0;
}
int main(void){
    httpd_req_t req={0};fixture();
    /* Simulate an older periodic snapshot still owning the flash mutex. A
       reset must neither mutate live data nor start another NVS snapshot. */
    flash_held=1;assert(energy_reset_handler(&req)==ESP_OK&&req.status==409);
    assert(energy_reset_wh[0]==100&&energy_reset_date==20260901&&!writes&&!gives&&takes==1);
    req.status=0;assert(sessions_reset_handler(&req)==ESP_OK&&req.status==409);
    assert(sessions.store.count==1&&sessions.store.records[0].id==42&&!writes&&!gives&&takes==2);
    flash_held=0;req.status=0;assert(sessions_reset_handler(&req)==ESP_OK&&req.status==200);
    assert(!sessions.store.count&&!persisted_sessions.count&&!flash_held&&gives==1&&commits==1);
    fixture();assert(energy_reset_handler(&req)==ESP_OK&&req.status==200);
    assert(energy_reset_wh[0]==12000&&persisted_reset[0]==12000&&persisted_reset[1]==11000&&persisted_date==20261023);
    assert(!flash_held&&takes==1&&gives==1&&commits==1);
    fixture();fail_write=true;assert(energy_reset_handler(&req)==ESP_OK&&req.status==500);
    assert(energy_reset_wh[0]==100&&energy_reset_wh[1]==90&&energy_reset_date==20260901&&!flash_held&&gives==1&&!commits);
    fixture();fail_write=true;assert(sessions_reset_handler(&req)==ESP_OK&&req.status==500);
    assert(sessions.store.count==1&&sessions.store.records[0].id==42&&!flash_held&&gives==1&&!commits);
    fixture();clock_valid=false;assert(energy_reset_handler(&req)==ESP_OK&&req.status==503&&!takes&&!writes);
    assert(!locks);
    puts("PASS: production reset handlers serialize mutation and NVS snapshots with periodic persistence; busy storage leaves live data unchanged; successful resets persist; failure rolls back and releases mutex");
}
