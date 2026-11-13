#include "vehicle_profiles.h"
#include "ems_core.h"
static settings_t settings;
#include "ems_features.h"
#include <assert.h>
#include <stdlib.h>
#include <string.h>
#include <stdio.h>
typedef int esp_err_t;
typedef int nvs_handle_t;
typedef struct {const char *body;int status;} httpd_req_t;
enum {ESP_OK=0,NVS_READWRITE=0,ESP_ERR_INVALID_STATE=1,HTTPD_400_BAD_REQUEST=400,HTTPD_500_INTERNAL_SERVER_ERROR=500,pdTRUE=1};
static vehicle_store_t vehicles={.version=1},stored;
static charge_plan_t charge_plan;
static unsigned vehicle_revision=1,commits;
static bool config_storage_ok=true,restarting,ota_in_progress,backup_in_progress,boot_validation_pending,storage_ok=true;
static int maintenance_flash_lock,locks;
static int64_t vehicle_soc_at[4];
#define LOCK() assert(locks++==0)
#define UNLOCK() assert(--locks==0)
static int ticks(int x){return x;}
static int xSemaphoreTake(int a,int b){(void)a;(void)b;return pdTRUE;}
static void xSemaphoreGive(int a){(void)a;}
static bool authorized(httpd_req_t *r){(void)r;return true;}
static cJSON *read_json(httpd_req_t *r){return cJSON_Parse(r->body);}
typedef struct {vehicle_store_t store;} vehicle_assets_t;
static vehicle_assets_t *vehicle_assets_read(void){vehicle_assets_t *p=malloc(sizeof(*p));if(p)p->store=vehicles;return p;}
static bool vehicle_assets_commit(const vehicle_assets_t *p){assert(!locks);commits++;if(storage_ok)stored=p->store;return storage_ok;}
static int httpd_resp_send_err(httpd_req_t *r,int status,const char *message){assert(message);r->status=status;return 0;}
static int httpd_resp_set_status(httpd_req_t *r,const char *status){r->status=!strncmp(status,"409",3)?409:400;return 0;}
static int httpd_resp_sendstr(httpd_req_t *r,const char *message){(void)r;assert(message);return 0;}
static cJSON *vehicle_snapshot(void){return cJSON_CreateObject();}
static int send_json(httpd_req_t *r,cJSON *o){cJSON_Delete(o);r->status=200;return 0;}
#include "vehicle_config_http_actual.inc"
int main(void){
 _Static_assert(sizeof(vehicle_profile_t)==192,"profile storage layout unchanged");
 httpd_req_t r={"{\"enabled\":true,\"revision\":1}",0};vehicles_post(&r);assert(r.status==200&&vehicles.enabled&&stored.enabled&&vehicle_revision==2&&commits==1);
 strcpy(vehicles.rows[0].name,"Test");strcpy(vehicles.rows[0].cards[0],"CARD");vehicles.rows[0].capacity_kwh=60;vehicles.rows[0].efficiency=.9;vehicles.rows[0].maximum_kw=11;vehicles.rows[0].target_soc=80;vehicles.rows[0].picture=3;
 r=(httpd_req_t){"{\"enabled\":false,\"revision\":2}",0};vehicles_post(&r);assert(r.status==200&&!vehicles.enabled&&vehicles.rows[0].picture==3&&!strcmp(vehicles.rows[0].name,"Test"));
 r=(httpd_req_t){"{\"enabled\":true,\"revision\":2}",0};vehicles_post(&r);assert(r.status==409&&!vehicles.enabled);
 storage_ok=false;r=(httpd_req_t){"{\"enabled\":true,\"revision\":3}",0};vehicles_post(&r);assert(r.status==500&&!vehicles.enabled&&vehicle_revision==3);
 storage_ok=true;charge_plan.active=true;r.status=0;vehicles_post(&r);assert(r.status==500&&!vehicles.enabled);
 assert(!locks);puts("PASS: actual feature endpoint enables empty profiles, preserves all saved profile data on disable, rejects stale revision, failed flash commit and active-plan changes");
}
