static bool personal_pin_valid(const char *pin){return pin&&!strcmp(pin,"3030");}
static cJSON *personal_snapshot(void){
    cJSON *o=cJSON_CreateObject();if(!o)return NULL;
    LOCK();personal_sync_locked(now_ms());
    cJSON_AddBoolToObject(o,"personal",true);cJSON_AddBoolToObject(o,"voltage_enabled",personal_voltage_enabled);
    cJSON_AddBoolToObject(o,"solis_enabled",personal_solis_enabled);
    bool valid=personal_voltage_enabled&&!strcmp(settings.wallbox_meter_type,"xemex")&&ems_phase_voltage_active();cJSON_AddBoolToObject(o,"voltage_fresh",valid);
    cJSON *a=cJSON_AddArrayToObject(o,"voltage_v");for(unsigned i=0;i<3;i++)json_add_owned_to_array(a,valid?cJSON_CreateNumber(personal_voltage[i]):cJSON_CreateNull());
    UNLOCK();return o;
}
static esp_err_t personal_post(httpd_req_t *req,cJSON *o){
    cJSON *pin=cJSON_GetObjectItemCaseSensitive(o,"pin"),*voltage=cJSON_GetObjectItemCaseSensitive(o,"voltage_enabled"),*solis=cJSON_GetObjectItemCaseSensitive(o,"solis_enabled");
    if(!cJSON_IsString(pin)||!personal_pin_valid(pin->valuestring)){expert_wrong_until=now_ms()+2000;cJSON_Delete(o);return httpd_resp_send_err(req,HTTPD_403_FORBIDDEN,"Passwort falsch.");}
    if(!cJSON_IsBool(voltage)||!cJSON_IsBool(solis)||cJSON_GetArraySize(o)!=4){cJSON_Delete(o);return httpd_resp_send_err(req,HTTPD_400_BAD_REQUEST,"Auswahl unvollstaendig.");}
    bool use_voltage=cJSON_IsTrue(voltage),use_solis=cJSON_IsTrue(solis);cJSON_Delete(o);
    if(!config_storage_ok||xSemaphoreTake(maintenance_flash_lock,ticks(1500))!=pdTRUE)return httpd_resp_send_err(req,HTTPD_500_INTERNAL_SERVER_ERROR,"Speicher nicht bereit.");
    LOCK();bool allowed=!restarting&&!ota_in_progress&&!backup_in_progress&&!boot_validation_pending;UNLOCK();
    nvs_handle_t nvs;esp_err_t err=allowed?nvs_open("personal",NVS_READWRITE,&nvs):ESP_ERR_INVALID_STATE;
    if(err==ESP_OK){err=nvs_set_u32(nvs,"options",(use_voltage?1u:0u)|(use_solis?2u:0u));if(err==ESP_OK)err=nvs_commit(nvs);nvs_close(nvs);}
    if(err==ESP_OK){LOCK();personal_voltage_enabled=use_voltage;personal_solis_enabled=use_solis;
        if(!use_voltage)personal_voltage_at=0;
        if(!use_solis&&solis_hold_wanted){solis_hold_wanted=false;solis_hold_release=true;solis_hold_seq++;solis_hold_sent=0;}
        personal_sync_locked(now_ms());UNLOCK();}
    xSemaphoreGive(maintenance_flash_lock);
    if(err!=ESP_OK)return httpd_resp_send_err(req,HTTPD_500_INTERNAL_SERVER_ERROR,"Nicht gespeichert. Erneut versuchen.");
    return send_json(req,personal_snapshot());
}
