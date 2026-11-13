static esp_err_t vehicles_post(httpd_req_t *req){
    if(!authorized(req))return ESP_OK;
    cJSON *o=read_json(req);if(!o)return ESP_OK;
    cJSON *slot=cJSON_GetObjectItem(o,"slot"),*enabled=cJSON_GetObjectItem(o,"enabled"),*revision=cJSON_GetObjectItem(o,"revision");
    unsigned index=cJSON_IsNumber(slot)?(unsigned)slot->valueint:VEHICLE_COUNT;
    bool feature_only=!slot&&!cJSON_GetObjectItem(o,"profile")&&cJSON_GetArraySize(o)==2;
    bool valid=cJSON_IsBool(enabled)&&cJSON_IsNumber(revision)&&(feature_only||
        (cJSON_IsNumber(slot)&&slot->valuedouble==slot->valueint&&index>=1&&index<=VEHICLE_COUNT&&cJSON_GetArraySize(o)==4));
    vehicle_store_t *next=malloc(sizeof(*next));if(!next){cJSON_Delete(o);return httpd_resp_send_err(req,HTTPD_500_INTERNAL_SERVER_ERROR,"Speicher belegt. Erneut versuchen.");}
    LOCK();*next=vehicles;bool unchanged=cJSON_IsNumber(revision)&&revision->valuedouble==vehicle_revision;UNLOCK();
    if(valid){next->enabled=cJSON_IsTrue(enabled);valid=(feature_only||vehicle_profile_parse(cJSON_GetObjectItem(o,"profile"),&next->rows[index-1]))&&vehicle_store_valid(next);}
    cJSON_Delete(o);
    LOCK();bool deleting_fixed=valid&&!feature_only&&settings.vehicle_fixed_profile==index&&!next->rows[index-1].name[0];UNLOCK();
    if(deleting_fixed){free(next);httpd_resp_set_status(req,"409 Conflict");return httpd_resp_sendstr(req,"Vor dem Loeschen ein anderes festes Profil oder Ladekartenerkennung waehlen und speichern.");}
    if(!valid||!unchanged){free(next);httpd_resp_set_status(req,unchanged?"400 Bad Request":"409 Conflict");return httpd_resp_sendstr(req,unchanged?"Profil unvollstaendig oder Ladekarte bereits zugeordnet.":"Profile wurden inzwischen geaendert. Neu laden.");}
    if(xSemaphoreTake(maintenance_flash_lock,ticks(1500))!=pdTRUE){free(next);return httpd_resp_send_err(req,HTTPD_500_INTERNAL_SERVER_ERROR,"Speicherung beschaeftigt.");}
    LOCK();bool allowed=config_storage_ok&&!charge_plan.active&&!restarting&&!ota_in_progress&&!backup_in_progress&&!boot_validation_pending;UNLOCK();
    vehicle_assets_t *assets=allowed?vehicle_assets_read():NULL;esp_err_t error=ESP_ERR_INVALID_STATE;
    if(assets){assets->store=*next;error=vehicle_assets_commit(assets)?ESP_OK:ESP_ERR_INVALID_STATE;free(assets);}
    if(error==ESP_OK){LOCK();vehicles=*next;vehicle_revision++;memset(vehicle_soc_at,0,sizeof(vehicle_soc_at));UNLOCK();}
    xSemaphoreGive(maintenance_flash_lock);free(next);
    if(error!=ESP_OK)return httpd_resp_send_err(req,HTTPD_500_INTERNAL_SERVER_ERROR,allowed?"Profil nicht gespeichert. Speicher pruefen.":"Ladeplan oder Wartung zuerst beenden.");
    return send_json(req,vehicle_snapshot());
}
