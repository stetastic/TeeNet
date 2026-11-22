/* Optional installation heartbeat. Never contains charging data, MAC or Wi-Fi identity. */
#include "telemetry_core.h"
#include "telemetry_endpoint.h"
static telemetry_config_t telemetry_config={.schema=1};
static SemaphoreHandle_t telemetry_lock;
static bool telemetry_last_ok;
static const char *telemetry_wait_reason;
static int telemetry_last_http;
static uint32_t telemetry_duration_ms;
static int64_t telemetry_last_at,telemetry_next_at;

static void telemetry_load(void){
    nvs_handle_t h;size_t size=sizeof(telemetry_config_t);telemetry_config_t saved;
    if(config_storage_ok&&nvs_open("wallbox_ems",NVS_READONLY,&h)==ESP_OK){
        if(nvs_get_blob(h,"telemetry_cfg",&saved,&size)==ESP_OK&&size==sizeof(saved)&&telemetry_valid(&saved))telemetry_config=saved;
        nvs_close(h);
    }
}
static bool telemetry_store(const telemetry_config_t *next){
    if(!config_storage_ok||xSemaphoreTake(maintenance_flash_lock,ticks(2000))!=pdTRUE)return false;
    nvs_handle_t h;esp_err_t err=nvs_open("wallbox_ems",NVS_READWRITE,&h);
    if(err==ESP_OK){err=nvs_set_blob(h,"telemetry_cfg",next,sizeof(*next));if(err==ESP_OK)err=nvs_commit(h);nvs_close(h);}
    xSemaphoreGive(maintenance_flash_lock);return err==ESP_OK;
}
static void telemetry_json(cJSON *o){
    /* Called while state_lock is held. Credentials stay exclusively in NVS/RAM. */
    cJSON_AddBoolToObject(o,"telemetry_enabled",telemetry_config.enabled);
    cJSON_AddBoolToObject(o,"telemetry_decided",telemetry_config.revision[0]!=0);
    cJSON_AddBoolToObject(o,"telemetry_delete_pending",telemetry_config.delete_pending);
    cJSON_AddBoolToObject(o,"telemetry_last_ok",telemetry_last_ok);
    cJSON_AddStringToObject(o,"telemetry_wait",telemetry_wait_reason?telemetry_wait_reason:"");
    cJSON_AddNumberToObject(o,"telemetry_last_http",telemetry_last_http);
    cJSON_AddNumberToObject(o,"telemetry_duration_ms",telemetry_duration_ms);
    cJSON_AddNumberToObject(o,"telemetry_last_age_s",telemetry_last_at?(now_ms()-telemetry_last_at)/1000:-1);
    cJSON_AddStringToObject(o,"telemetry_revision",TELEMETRY_REVISION);
}
static esp_err_t telemetry_post(httpd_req_t *req){
    if(!authorized(req))return ESP_OK;
    if(req->content_len>128)return httpd_resp_send_err(req,HTTPD_400_BAD_REQUEST,"Auswahl zu gross.");
    cJSON *o=read_json(req);if(!o)return ESP_OK;
    cJSON *enabled=cJSON_GetObjectItemCaseSensitive(o,"enabled"),*rev=cJSON_GetObjectItemCaseSensitive(o,"revision");
    unsigned fields=0;for(cJSON *p=o->child;p;p=p->next)fields++;
    LOCK();bool agreed=terms_accepted;UNLOCK();
    bool valid=fields==2&&cJSON_IsBool(enabled)&&cJSON_IsString(rev)&&!strcmp(rev->valuestring,TELEMETRY_REVISION)&&(!cJSON_IsTrue(enabled)||agreed);
    bool enable=cJSON_IsTrue(enabled);cJSON_Delete(o);
    if(!valid)return httpd_resp_send_err(req,HTTPD_400_BAD_REQUEST,"Hinweis zur Geraetestatistik bestaetigen.");
    if(!telemetry_lock||xSemaphoreTake(telemetry_lock,ticks(7000))!=pdTRUE)return httpd_resp_send_err(req,HTTPD_500_INTERNAL_SERVER_ERROR,"Statistik beschaeftigt. Erneut versuchen.");
    LOCK();telemetry_config_t next=telemetry_config;UNLOCK();
    next.schema=1;strcpy(next.revision,TELEMETRY_REVISION);
    if(enable){
        if(!telemetry_hex(next.id,32)||!telemetry_hex(next.key,64)){
            uint8_t random[48];esp_fill_random(random,sizeof(random));
            for(unsigned i=0;i<16;i++)snprintf(next.id+i*2,3,"%02x",random[i]);
            for(unsigned i=0;i<32;i++)snprintf(next.key+i*2,3,"%02x",random[i+16]);
            memset(random,0,sizeof(random));
        }
        next.enabled=1;next.delete_pending=0;
    }else telemetry_revoke(&next);
    /* Revoke in memory even if NVS fails. The UI explicitly reports a failed persistent save. */
    if(!enable){LOCK();telemetry_config=next;UNLOCK();}
    bool stored=telemetry_valid(&next)&&telemetry_store(&next);
    if(stored){LOCK();telemetry_config=next;telemetry_next_at=0;telemetry_last_ok=false;telemetry_wait_reason=NULL;telemetry_last_http=0;telemetry_duration_ms=0;UNLOCK();}
    xSemaphoreGive(telemetry_lock);
    if(!stored)return httpd_resp_send_err(req,HTTPD_500_INTERNAL_SERVER_ERROR,"Statistikauswahl nicht dauerhaft gespeichert. Erneut versuchen.");
    return ok_json(req);
}
#include "telemetry_worker.inc"
