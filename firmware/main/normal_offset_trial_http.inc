/* Temporary normal-controller offset; original saved settings never change.
   Expiry latches Stop; zero proof is required before releasing the test. */
static esp_err_t normal_offset_trial_post(httpd_req_t *req,cJSON *o){
    const cJSON *pin=cJSON_GetObjectItemCaseSensitive(o,"pin"),*action=cJSON_GetObjectItemCaseSensitive(o,"action"),
        *offset=cJSON_GetObjectItemCaseSensitive(o,"offset_a");
    bool begin=cJSON_IsString(action)&&!strcmp(action->valuestring,"offset_begin");
    bool keep=cJSON_IsString(action)&&!strcmp(action->valuestring,"offset_keepalive");
    bool end=cJSON_IsString(action)&&!strcmp(action->valuestring,"offset_end");
    bool valid=(begin||keep||end)&&cJSON_IsString(pin)&&expert_pin_valid(pin->valuestring)&&
        (begin?(cJSON_IsNumber(offset)&&isfinite(offset->valuedouble)):(offset==NULL));
    for(const cJSON *p=o->child;valid&&p;p=p->next){
        if(strcmp(p->string,"pin")&&strcmp(p->string,"action")&&
           strcmp(p->string,"offset_a")&&strcmp(p->string,"confirm_risk"))valid=false;
        for(const cJSON *q=p->next;q;q=q->next)if(!strcmp(p->string,q->string))valid=false;
    }
    if(!valid){cJSON_Delete(o);return httpd_resp_send_err(req,HTTPD_403_FORBIDDEN,"Offset-Testaktion ungueltig.");}
    LOCK();int64_t now=now_ms();
    bool zero=fresh(now,actual_at,wallbox_meter_ttl())&&phase_measured_zero(&settings,actual_a,1);
    bool idle=!settings.enabled&&!evcc_control.enabled&&zero&&phase_state==PHASE_READY;
    bool accepted=false;
    if(begin){
        bool ready=idle&&!settings.shell_http_control_enabled&&!shell_http_trial.armed&&!github_update.running&&!restarting&&!ota_in_progress&&!restart_pending()&&
            !phase_test_active&&!charge_plan.active&&!evcc_control.configured&&
            cJSON_IsTrue(cJSON_GetObjectItemCaseSensitive(o,"confirm_risk"));
        if(ready&&normal_offset_trial_begin(&normal_offset_trial,settings.current_offset_a,(float)offset->valuedouble,now)){
            settings.current_offset_a=(float)offset->valuedouble;accepted=true;
            memset(&manual_filter,0,sizeof(manual_filter));memset(&current_calibration,0,sizeof(current_calibration));
        }
    }else if(keep)accepted=normal_offset_trial_touch(&normal_offset_trial,now);
    else if(end){
        settings.enabled=false;settings.mode=MODE_OFF;accepted=true;
        if(normal_offset_trial.armed)normal_offset_trial.fault=true;
        if(idle&&normal_offset_trial.armed){
            settings.current_offset_a=normal_offset_trial.original;
            memset(&normal_offset_trial,0,sizeof(normal_offset_trial));
            memset(&manual_filter,0,sizeof(manual_filter));memset(&current_calibration,0,sizeof(current_calibration));
        }
    }
    UNLOCK();cJSON_Delete(o);
    if(!accepted){httpd_resp_set_status(req,"409 Conflict");return httpd_resp_sendstr(req,"Offset-Test nur bei Stillstand; Wert hoechstens 0,2 A vom Ausgangswert aendern.");}
    return ok_json(req);
}
