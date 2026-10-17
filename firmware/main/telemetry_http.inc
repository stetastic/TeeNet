/* Optional installation heartbeat. Never contains charging data, MAC or Wi-Fi identity. */
#include "telemetry_core.h"
#include "telemetry_endpoint.h"
static telemetry_config_t telemetry_config={.schema=1};
static SemaphoreHandle_t telemetry_lock;
static bool telemetry_last_ok;
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
    if(stored){LOCK();telemetry_config=next;telemetry_next_at=0;telemetry_last_ok=false;UNLOCK();}
    xSemaphoreGive(telemetry_lock);
    if(!stored)return httpd_resp_send_err(req,HTTPD_500_INTERNAL_SERVER_ERROR,"Statistikauswahl nicht dauerhaft gespeichert. Erneut versuchen.");
    return ok_json(req);
}
static void telemetry_task(void *arg){
    (void)arg;
    vTaskDelay(ticks(15000));
    for(;;){
        vTaskDelay(ticks(5000));
        if(!TELEMETRY_ENDPOINT[0]||!telemetry_lock||xSemaphoreTake(telemetry_lock,0)!=pdTRUE)continue;
        int64_t now=now_ms();LOCK();telemetry_config_t c=telemetry_config;
        bool due=telemetry_due(&c,terms_accepted,wifi_online,ota_in_progress||restarting,now,telemetry_next_at);UNLOCK();
        if(!due||time(NULL)<1735689600||esp_get_free_heap_size()<60000){xSemaphoreGive(telemetry_lock);continue;}
        char body[288],auth[73];
        if(c.delete_pending)snprintf(body,sizeof(body),"{\"id\":\"%s\"}",c.id);
        else snprintf(body,sizeof(body),"{\"id\":\"%s\",\"version\":\"%s\",\"build\":\"%s\",\"hardware\":\"%s\",\"consent_revision\":\"%s\"}",c.id,VERSION,EMS_BUILD_ID,EMS_HARDWARE_ID,TELEMETRY_REVISION);
        snprintf(auth,sizeof(auth),"Bearer %s",c.key);
        esp_http_client_config_t cfg={.url=TELEMETRY_ENDPOINT,.method=c.delete_pending?HTTP_METHOD_DELETE:HTTP_METHOD_POST,
            .timeout_ms=5000,.crt_bundle_attach=esp_crt_bundle_attach,.disable_auto_redirect=true,.buffer_size=512,.buffer_size_tx=512};
        esp_http_client_handle_t client=esp_http_client_init(&cfg);bool ok=false;
        if(client){
            esp_http_client_set_header(client,"Content-Type","application/json");
            esp_http_client_set_header(client,"Authorization",auth);
            esp_http_client_set_header(client,"User-Agent","TeeNet-installation-statistics");
            esp_http_client_set_post_field(client,body,strlen(body));
            esp_err_t err=esp_http_client_perform(client);int status=esp_http_client_get_status_code(client);
            ok=err==ESP_OK&&status>=200&&status<300;
            esp_http_client_cleanup(client);
        }
        memset(auth,0,sizeof(auth));
        if(ok&&c.delete_pending){
            c.delete_pending=0;memset(c.id,0,sizeof(c.id));memset(c.key,0,sizeof(c.key));
            if(telemetry_store(&c)){LOCK();telemetry_config=c;UNLOCK();}
        }
        LOCK();telemetry_last_ok=ok;if(ok)telemetry_last_at=now_ms();
        telemetry_next_at=now_ms()+TELEMETRY_INTERVAL_MS+(esp_random()%15000);UNLOCK();
        memset(&c,0,sizeof(c));xSemaphoreGive(telemetry_lock);
    }
}
