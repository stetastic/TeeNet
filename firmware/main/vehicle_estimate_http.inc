static esp_err_t vehicle_estimate_post(httpd_req_t *req){
    if(!authorized(req))return ESP_OK;
    cJSON *o=read_json(req);if(!o)return ESP_OK;
    const cJSON *slot=cJSON_GetObjectItem(o,"slot"),*start=cJSON_GetObjectItem(o,"start_soc");
    bool valid=cJSON_GetArraySize(o)==2&&cJSON_IsNumber(slot)&&slot->valuedouble==slot->valueint&&
        slot->valueint>=1&&slot->valueint<=VEHICLE_COUNT&&cJSON_IsNumber(start)&&isfinite(start->valuedouble)&&
        start->valuedouble>=0&&start->valuedouble<=100;
    LOCK();
    if(valid){unsigned index=(unsigned)slot->valueint-1;
        valid=vehicles.enabled&&vehicle_current_locked(now_ms())==(int)index&&!settings.basic_mode&&!restarting&&!ota_in_progress&&!backup_in_progress&&!boot_validation_pending&&
            (vehicle_soc_fresh_locked(index,now_ms())||vehicle_estimate_begin_locked(index,(float)start->valuedouble,now_ms()));
    }
    UNLOCK();cJSON_Delete(o);
    if(!valid)return httpd_resp_send_err(req,HTTPD_400_BAD_REQUEST,"Fahrzeugprofil fest auswaehlen oder per Ladekarte erkennen lassen; angeschlossenes Fahrzeug und Startwert 0 bis 100 Prozent pruefen.");
    return send_json(req,vehicle_snapshot());
}
