static esp_err_t solis_hold_post(httpd_req_t *req){
    if(!authorized(req))return ESP_OK;
    cJSON *o=read_json(req);if(!o)return ESP_OK;
    cJSON *selected=cJSON_GetObjectItemCaseSensitive(o,"selected");
    bool valid=cJSON_IsBool(selected)&&cJSON_GetArraySize(o)==1,wanted=cJSON_IsTrue(selected);cJSON_Delete(o);
    if(!valid)return httpd_resp_send_err(req,HTTPD_400_BAD_REQUEST,"Nur selected als true oder false senden.");
    LOCK();bool allowed=!wanted||(personal_solis_enabled&&solis_hold_link_locked(now_ms())&&!solis_hold_manual&&
        !settings.basic_mode&&settings.battery_protect&&!restart_pending()&&!ota_in_progress&&!backup_in_progress);
    if(allowed){solis_hold_wanted=wanted;solis_hold_release=!wanted;solis_hold_error=false;solis_hold_seq++;solis_hold_sent=0;}
    UNLOCK();
    if(!allowed){httpd_resp_set_status(req,"409 Conflict");return httpd_resp_sendstr(req,
        "Solis-Anbindung nicht bereit oder Sperre bereits in VIS gewaehlt. MQTT und ioBroker pruefen.");}
    return ok_json(req);
}
