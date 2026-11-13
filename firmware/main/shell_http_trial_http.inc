/* Private local trial, volatile opt-in. Does not alter saved configuration,
   expert defaults, current minimum, phase interlocks or public UI. */
#include "normal_offset_trial_http.inc"
static esp_err_t shell_http_trial_post(httpd_req_t *req){
    if(!authorized(req))return ESP_OK;
    cJSON *o=read_json(req);if(!o)return ESP_OK;
    const cJSON *pin=cJSON_GetObjectItemCaseSensitive(o,"pin"),*action=cJSON_GetObjectItemCaseSensitive(o,"action");
    if(cJSON_IsString(action)&&!strncmp(action->valuestring,"offset_",7))return normal_offset_trial_post(req,o);
    bool valid=cJSON_IsString(pin)&&expert_pin_valid(pin->valuestring)&&cJSON_IsString(action);
    for(const cJSON *p=o->child;valid&&p;p=p->next){
        if(strcmp(p->string,"pin")&&strcmp(p->string,"action")&&strcmp(p->string,"confirm_risk")&&
           strcmp(p->string,"step_a")&&strcmp(p->string,"bias_a")&&strcmp(p->string,"interval_s")&&strcmp(p->string,"pulse_s"))valid=false;
        for(const cJSON *q=p->next;q;q=q->next)if(!strcmp(p->string,q->string))valid=false;
    }
    const cJSON *step=cJSON_GetObjectItemCaseSensitive(o,"step_a"),*bias=cJSON_GetObjectItemCaseSensitive(o,"bias_a"),
        *interval=cJSON_GetObjectItemCaseSensitive(o,"interval_s"),*pulse=cJSON_GetObjectItemCaseSensitive(o,"pulse_s");
    float step_value=step&&cJSON_IsNumber(step)?step->valuedouble:1,bias_value=bias&&cJSON_IsNumber(bias)?bias->valuedouble:.25f;
    int interval_value=interval&&cJSON_IsNumber(interval)?interval->valueint:12,pulse_value=pulse&&cJSON_IsNumber(pulse)?pulse->valueint:6;
    valid=valid&&(!step||cJSON_IsNumber(step))&&(!bias||cJSON_IsNumber(bias))&&(!interval||cJSON_IsNumber(interval))&&(!pulse||cJSON_IsNumber(pulse))&&
        isfinite(step_value)&&step_value>=.25f&&step_value<=1.5f&&isfinite(bias_value)&&bias_value>=-.5f&&bias_value<=.8f&&
        interval_value>=6&&interval_value<=20&&pulse_value>=2&&pulse_value<=6&&interval_value>=pulse_value+2&&
        (!interval||interval->valuedouble==interval_value)&&(!pulse||pulse->valuedouble==pulse_value);
    if(valid&&strcmp(action->valuestring,"arm")&&(step||bias||interval||pulse))valid=false;
    if(!valid){cJSON_Delete(o);return httpd_resp_send_err(req,HTTPD_403_FORBIDDEN,"Testaktion oder Experten-Passwort ungueltig.");}
    LOCK();int64_t now=now_ms();bool accepted=false;
    bool zero=fresh(now,actual_at,wallbox_meter_ttl())&&phase_measured_zero(&settings,actual_a,1);
    bool idle=!settings.enabled&&!evcc_control.enabled&&zero&&phase_state==PHASE_READY;
    if(!strcmp(action->valuestring,"arm")){
        accepted=!settings.shell_http_control_enabled&&!shell_http_trial.armed&&!normal_offset_trial.armed&&!github_update.running&&idle&&!restarting&&!ota_in_progress&&!restart_pending()&&!phase_test_active&&
            !evcc_control.configured&&settings.shell_setup_host[0]&&shell_vehicle_fresh(&evcc_shell,evcc_shell_at,now)&&
            evcc_shell.state!='F'&&shell_http_trial_fresh(&evcc_shell.http_control,evcc_shell_at,now)&&
            evcc_shell.http_control.pilot_a<.1f&&cJSON_IsTrue(cJSON_GetObjectItemCaseSensitive(o,"confirm_risk"));
        if(accepted){shell_http_trial_arm(&shell_http_trial,now);shell_http_trial.normal_offset_a=settings.current_offset_a;shell_http_trial.step_a=step_value;shell_http_trial.pilot_bias_a=bias_value;
            shell_http_trial.interval_ms=interval_value*1000;shell_http_trial.pulse_ms=pulse_value*1000;
            evcc_poll_generation++;memset(&manual_filter,0,sizeof(manual_filter));}
    }else if(!strcmp(action->valuestring,"keepalive")){
        accepted=shell_http_trial.armed&&!shell_http_trial.fault&&now<shell_http_trial.lease_until;
        if(accepted)shell_http_trial.lease_until=now+SHELL_HTTP_TRIAL_LEASE_MS;
    }else if(!strcmp(action->valuestring,"stop")){
        settings.enabled=false;settings.mode=MODE_OFF;
        accepted=true;
        /* An expired/failed trial cannot fall back to the normal regulator
           while current is flowing. Release only after independent zero. */
        if(idle){memset(&shell_http_trial,0,sizeof(shell_http_trial));memset(&manual_filter,0,sizeof(manual_filter));}
    }
    UNLOCK();cJSON_Delete(o);
    if(!accepted){httpd_resp_set_status(req,"409 Conflict");return httpd_resp_sendstr(req,"Test nicht bereit: stoppen, aktuelle HTTP-Daten und Stromstillstand pruefen.");}
    return ok_json(req);
}
