/* Separate authenticated API: never reachable via MQTT or evcc commands. */
static esp_err_t phase_test_handler(httpd_req_t *req){
    if(!authorized(req))return ESP_OK;
    cJSON *o=read_json(req);if(!o)return ESP_OK;
    cJSON *goal=cJSON_GetObjectItemCaseSensitive(o,"phases"),
        *pin=cJSON_GetObjectItemCaseSensitive(o,"pin"),
        *risk=cJSON_GetObjectItemCaseSensitive(o,"confirm_risk"),
        *stop=cJSON_GetObjectItemCaseSensitive(o,"stop");
    bool cancel=cJSON_IsTrue(stop),valid=cJSON_IsObject(o);
    if(!cancel&&now_ms()<expert_wrong_until){cJSON_Delete(o);httpd_resp_set_status(req,"429 Too Many Requests");return httpd_resp_sendstr(req,"Kurz warten und erneut versuchen.");}
    for(cJSON *i=o->child;i&&valid;i=i->next){
        if(strcmp(i->string,"phases")&&strcmp(i->string,"pin")&&strcmp(i->string,"confirm_risk")&&strcmp(i->string,"stop"))valid=false;
        for(cJSON *j=i->next;j;j=j->next)if(!strcmp(i->string,j->string))valid=false;
    }
    if(cancel)valid=valid&&cJSON_GetArraySize(o)==1;
    else valid=valid&&(!stop||cJSON_IsFalse(stop))&&cJSON_IsNumber(goal)&&
        (goal->valuedouble==1||goal->valuedouble==3)&&cJSON_IsTrue(risk)&&
        cJSON_IsString(pin)&&expert_pin_valid(pin->valuestring);
    if(!cancel&&cJSON_IsString(pin)&&!expert_pin_valid(pin->valuestring))expert_wrong_until=now_ms()+2000;
    unsigned phases=valid&&!cancel?(unsigned)goal->valueint:0;
    cJSON_Delete(o);
    if(!valid)return httpd_resp_send_err(req,HTTPD_400_BAD_REQUEST,"Warnung bestaetigen und Passwort 4040 eingeben. Eine oder drei Phasen waehlen.");
    LOCK();
    if(cancel){phase_test_end_locked();UNLOCK();return ok_json(req);}
    const char *blocked=phase_test_block_locked(now_ms());bool allowed=!blocked[0];
    if(allowed){
        settings.enabled=false;settings.mode=MODE_OFF;evcc_local_stop(&evcc_control);
        phase_test_active=true;phase_test_goal=phases;phase_test_until=now_ms()+90000;
        phase_startup=false;phase_goal=phases;phase_state=PHASE_STOPPING;
        phase_stop_at=now_ms();phase_zero_at=0;phase_hold_until=0;
        phase_idle_inhibit=false;phase_attempt_at=phase_no_load_since=0;
        event_locked("phase_hardware_test");
    }
    UNLOCK();
    if(!allowed){httpd_resp_set_status(req,"409 Conflict");return httpd_resp_sendstr(req,blocked);}
    return ok_json(req);
}
