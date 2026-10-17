/* Local terms acceptance. Optional heartbeat consent is saved separately by telemetry_post. */
static void terms_load(void) {
    nvs_handle_t handle;char revision[32];size_t size=sizeof(revision);
    if(config_storage_ok && nvs_open("wallbox_ems",NVS_READONLY,&handle)==ESP_OK){
        terms_accepted=nvs_get_str(handle,"terms_rev",revision,&size)==ESP_OK && !strcmp(revision,TERMS_REVISION);
        nvs_close(handle);
    }
}
static esp_err_t terms_post(httpd_req_t *req) {
    if(!authorized(req))return ESP_OK;
    if(req->content_len>128)return httpd_resp_send_err(req,HTTPD_400_BAD_REQUEST,"Bestaetigung zu gross.");
    cJSON *o=read_json(req);if(!o)return ESP_OK;
    cJSON *accepted=cJSON_GetObjectItemCaseSensitive(o,"accepted"),*revision=cJSON_GetObjectItemCaseSensitive(o,"revision");
    unsigned count=0;for(cJSON *i=o->child;i;i=i->next)count++;
    bool valid=count==2 && cJSON_IsTrue(accepted) && cJSON_IsString(revision) && !strcmp(revision->valuestring,TERMS_REVISION);
    cJSON_Delete(o);
    if(!valid)return httpd_resp_send_err(req,HTTPD_400_BAD_REQUEST,"Aktuelle Nutzungsbedingungen bestaetigen.");
    nvs_handle_t handle;esp_err_t err=config_storage_ok?nvs_open("wallbox_ems",NVS_READWRITE,&handle):ESP_FAIL;
    if(err==ESP_OK){err=nvs_set_str(handle,"terms_rev",TERMS_REVISION);if(err==ESP_OK)err=nvs_commit(handle);nvs_close(handle);}
    if(err!=ESP_OK)return httpd_resp_send_err(req,HTTPD_500_INTERNAL_SERVER_ERROR,"Bestaetigung nicht gespeichert.");
    LOCK();terms_accepted=true;settings.enabled=false;settings.mode=MODE_OFF;UNLOCK();return ok_json(req);
}
