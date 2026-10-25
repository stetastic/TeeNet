static esp_err_t expert_post(httpd_req_t *req){
    if(!authorized(req))return ESP_OK;
    cJSON *o=read_json(req);if(!o)return ESP_OK;
    cJSON *pin=cJSON_GetObjectItemCaseSensitive(o,"pin"),*action=cJSON_GetObjectItemCaseSensitive(o,"action");
    /* Authenticated local users may always revoke; enabling still needs both
       password checks and warnings. A duplicate enabled field is rejected by
       phase_mqtt_post before any permission is changed. */
    if(cJSON_IsString(action)&&!strcmp(action->valuestring,"mqtt_phase_output")&&
       cJSON_IsFalse(cJSON_GetObjectItemCaseSensitive(o,"enabled")))return phase_mqtt_post(req,o);
    if(now_ms()<expert_wrong_until){cJSON_Delete(o);httpd_resp_set_status(req,"429 Too Many Requests");return httpd_resp_sendstr(req,"Kurz warten und erneut versuchen.");}
    if(!cJSON_IsString(pin)||!expert_pin_valid(pin->valuestring)){
        expert_wrong_until=now_ms()+2000;cJSON_Delete(o);
        return httpd_resp_send_err(req,HTTPD_403_FORBIDDEN,"Experten-Passwort falsch.");
    }
    if(!cJSON_IsString(action)){cJSON_Delete(o);return httpd_resp_send_err(req,HTTPD_400_BAD_REQUEST,"Aktion fehlt.");}
    /* The protected phase setup only needs to verify the repeated PIN.  Do
       not allocate the large parameter/GPIO snapshot a second time. */
    if(!strcmp(action->valuestring,"authorize")){cJSON_Delete(o);return ok_json(req);}
    if(!strcmp(action->valuestring,"read")){cJSON_Delete(o);return send_json(req,expert_values_snapshot());}
    if(!strcmp(action->valuestring,"read_gpio")){cJSON_Delete(o);return send_json(req,gpio_snapshot());}
    if(boot_validation_pending){cJSON_Delete(o);httpd_resp_set_status(req,"409 Conflict");return httpd_resp_sendstr(req,"Firmware wird geprueft. Danach erneut versuchen.");}
    if(!strcmp(action->valuestring,"mqtt_phase_output"))return phase_mqtt_post(req,o);
    if(!strcmp(action->valuestring,"gpio"))return gpio_post(req,o);
    cJSON_Delete(o); return httpd_resp_send_err(req,400,"Unsupported test action");
}
