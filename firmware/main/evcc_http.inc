/* Private test API. No synchronous Shell requests in the HTTP server. */
static bool evcc_key_authorized(httpd_req_t *req) {
    char supplied[80],expected[80];
    LOCK();bool active=evcc_control.configured && settings.evcc_feature_enabled && terms_accepted;
    snprintf(expected,sizeof(expected),"Bearer %s",evcc_key);UNLOCK();
    if(!active) {httpd_resp_set_status(req,"409 Conflict");httpd_resp_sendstr(req,"evcc-Testmodus ist aus.");return false;}
    if(httpd_req_get_hdr_value_str(req,"Authorization",supplied,sizeof(supplied))!=ESP_OK || strcmp(supplied,expected)) {
        httpd_resp_set_status(req,"401 Unauthorized");httpd_resp_set_hdr(req,"WWW-Authenticate","Bearer");
        httpd_resp_sendstr(req,"evcc-Schluessel fehlt oder ist ungueltig.");return false;
    }
    return true;
}

static void evcc_load(void) {
    bool active=false;nvs_handle_t handle;
    if(config_storage_ok && nvs_open("teenet_evcc",NVS_READONLY,&handle)==ESP_OK) {
        size_t size=sizeof(evcc_key);uint8_t enabled=0;
        bool valid=nvs_get_str(handle,"key",evcc_key,&size)==ESP_OK && strlen(evcc_key)==32;
        size=sizeof(evcc_host);valid=valid && nvs_get_str(handle,"host",evcc_host,&size)==ESP_OK && evcc_host_valid(evcc_host);
        if(valid && nvs_get_u8(handle,"enabled",&enabled)==ESP_OK)active=enabled==1;
        nvs_close(handle);
    }
    if(!evcc_key[0] || strlen(evcc_key)!=32) {
        uint32_t random[4];esp_fill_random(random,sizeof(random));
        snprintf(evcc_key,sizeof(evcc_key),"%08lx%08lx%08lx%08lx",(unsigned long)random[0],(unsigned long)random[1],(unsigned long)random[2],(unsigned long)random[3]);
    }
    if(!evcc_host_valid(evcc_host) && evcc_host_valid(settings.shell_setup_host))strcpy(evcc_host,settings.shell_setup_host);
    if(loaded_settings_version && loaded_settings_version<27 && active){settings.evcc_feature_enabled=true;saved_settings.evcc_feature_enabled=true;}
    evcc_configure(&evcc_control,active && settings.evcc_feature_enabled); /* Enable/current commands never survive a reboot. */
}

static bool evcc_ready_locked(int64_t now) {
    return terms_accepted && evcc_shell_fresh(&evcc_shell,evcc_shell_at,now) && evcc_shell.state!='F' &&
        feedback_ready(now) && fresh(now,meter_at,wallbox_meter_ttl()) && fresh(now,actual_at,wallbox_meter_ttl()) &&
        wallbox_ready && fresh(now,wallbox_at,ems_param_ms(EP_SHELL_TTL)) && !phase_config_saving && !phase_test_active && !restart_pending() && !restarting && !ota_in_progress && !phase_mqtt_saving &&
        !charge_guard_blocked(&charge_guard,now) && (!settings.phase_switch_enabled || (phase_gpio_ready && phase_state!=PHASE_FAULT && !phase_idle_inhibit));
}

static esp_err_t evcc_config_get(httpd_req_t *req) {
    if(!authorized(req))return ESP_OK;
    cJSON *o=cJSON_CreateObject();if(!o)return send_json(req,NULL);
    LOCK();cJSON_AddBoolToObject(o,"enabled",evcc_control.configured);
    cJSON_AddStringToObject(o,"shell_host",evcc_host);cJSON_AddStringToObject(o,"key",evcc_key);
    cJSON_AddBoolToObject(o,"phase_switch_supported",settings.phase_switch_enabled && phase_gpio_ready);
    cJSON_AddNumberToObject(o,"min_current_a",active_min_a());cJSON_AddNumberToObject(o,"max_current_a",settings.max_charge_a);
    cJSON_AddNumberToObject(o,"phases",settings.charge_phases);UNLOCK();return send_json(req,o);
}

static esp_err_t evcc_config_post(httpd_req_t *req) {
    if(!authorized(req))return ESP_OK;
    cJSON *o=read_json(req);if(!o)return ESP_OK;
    cJSON *enabled=cJSON_GetObjectItemCaseSensitive(o,"enabled"),*host=cJSON_GetObjectItemCaseSensitive(o,"shell_host");
    bool valid=cJSON_IsBool(enabled)&&cJSON_IsString(host)&&evcc_host_valid(host->valuestring);
    unsigned fields=0;for(cJSON *i=o->child;i;i=i->next)fields++;
    valid=valid&&fields==2;bool active=valid&&cJSON_IsTrue(enabled);char selected[64]={0};
    if(valid)strcpy(selected,host->valuestring);
    cJSON_Delete(o);
    if(!valid)return httpd_resp_send_err(req,HTTPD_400_BAD_REQUEST,"Shell-Adresse und evcc-Auswahl pruefen.");
    LOCK();bool stopped=!settings.enabled && fresh(now_ms(),actual_at,wallbox_meter_ttl()) &&
        actual_a[0]<1 && actual_a[1]<1 && actual_a[2]<1 && (!settings.phase_switch_enabled || phase_state==PHASE_READY);
    bool ready=settings.evcc_feature_enabled&&!reboot_required&&!restarting&&!ota_in_progress;
    if(!active || (stopped && ready))evcc_configuring=true;
    UNLOCK();
    if(active && (!stopped || !ready)) {
        httpd_resp_set_status(req,"409 Conflict");return httpd_resp_sendstr(req,"evcc nur bei gestoppter Ladung mit frischen Messwerten aktivieren.");
    }
    nvs_handle_t handle;esp_err_t error=config_storage_ok?nvs_open("teenet_evcc",NVS_READWRITE,&handle):ESP_FAIL;
    if(error==ESP_OK) {
        error=nvs_set_str(handle,"key",evcc_key);
        if(error==ESP_OK)error=nvs_set_str(handle,"host",selected);
        if(error==ESP_OK)error=nvs_set_u8(handle,"enabled",active?1:0);
        if(error==ESP_OK)error=nvs_commit(handle);
        nvs_close(handle);
    }
    if(error!=ESP_OK){LOCK();evcc_configuring=false;UNLOCK();return httpd_resp_send_err(req,HTTPD_500_INTERNAL_SERVER_ERROR,"evcc-Einstellung nicht gespeichert.");}
    LOCK();strcpy(evcc_host,selected);evcc_configure(&evcc_control,active);evcc_configuring=false;
    evcc_shell=(evcc_shell_t){.state='?'};strcpy(evcc_shell.reason,"pending");evcc_shell_at=0;evcc_poll_generation++;
    settings.enabled=false;settings.mode=MODE_OFF;
    phase_idle_inhibit=false;
    if(charge_plan.active){charge_plan.active=false;plan_phase=PLAN_OFF;plan_dirty=true;plan_revision++;}
    event_locked(active?"evcc_owner":"evcc_off");UNLOCK();return evcc_config_get(req);
}

static esp_err_t evcc_status_get(httpd_req_t *req) {
    if(!evcc_key_authorized(req))return ESP_OK;
    cJSON *o=cJSON_CreateObject();if(!o)return send_json(req,NULL);
    LOCK();int64_t now=now_ms();evcc_touch(&evcc_control,now);
    bool known=evcc_shell_fresh(&evcc_shell,evcc_shell_at,now);
    bool meter_ok=feedback_ready(now)&&fresh(now,meter_at,wallbox_meter_ttl())&&fresh(now,actual_at,wallbox_meter_ttl());
    char status[2]={known?evcc_shell.state:'?',0};
    cJSON_AddStringToObject(o,"status",status);cJSON_AddStringToObject(o,"raw_statuscar",evcc_shell.raw);
    cJSON_AddStringToObject(o,"status_reason",evcc_shell_at && now-evcc_shell_at>=EVCC_SHELL_TTL_MS?"shell_status_stale":evcc_shell.reason);
    cJSON_AddBoolToObject(o,"status_known",known);cJSON_AddBoolToObject(o,"meter_ok",meter_ok);
    cJSON_AddBoolToObject(o,"enabled",evcc_control.enabled && !evcc_control.expired && !evcc_control.inhibited &&
        settings.enabled && target_locked(now)>0);
    cJSON_AddBoolToObject(o,"requested",evcc_control.enabled);
    cJSON_AddBoolToObject(o,"local_stop",evcc_control.inhibited);cJSON_AddBoolToObject(o,"lease_expired",evcc_control.expired);
    cJSON_AddStringToObject(o,"block_reason",block_reason_locked(now));
    cJSON_AddNumberToObject(o,"phases",settings.charge_phases);
    cJSON_AddNumberToObject(o,"requested_phases",evcc_desired_phases(&evcc_control,settings.charge_phases));
    cJSON_AddBoolToObject(o,"phase_switching",settings.phase_switch_enabled && phase_state!=PHASE_READY);
    cJSON_AddBoolToObject(o,"phase_switch_supported",settings.phase_switch_enabled && phase_gpio_ready);
    cJSON_AddNumberToObject(o,"min_current_a",active_min_a());cJSON_AddNumberToObject(o,"max_current_a",settings.max_charge_a);
    cJSON_AddNumberToObject(o,"requested_current_a",evcc_control.current_set?evcc_control.current_a:0);
    bool idle=meter_ok && actual_a[0]<1 && actual_a[1]<1 && actual_a[2]<1;
    nullable(o,"power_w",meter_ok&&!idle?estimated_charge_power(&settings,meter_a):0,meter_ok);
    cJSON_AddNumberToObject(o,"energy_kwh",energy.store.total_wh[0]/1000.0);
    nullable(o,"shell_age_s",evcc_shell_at?(now-evcc_shell_at)/1000.0:0,evcc_shell_at>0);
    cJSON_AddNumberToObject(o,"shell_http_code",evcc_shell_http_code);UNLOCK();
    /* Do not refresh an expired lease back into an enabled state. */
    if(!known){httpd_resp_set_status(req,"503 Service Unavailable");httpd_resp_set_hdr(req,"Retry-After","5");}
    return send_json(req,o);
}

static esp_err_t evcc_enabled_get(httpd_req_t *req) {
    if(!evcc_key_authorized(req))return ESP_OK;
    LOCK();int64_t now=now_ms();evcc_touch(&evcc_control,now);
    bool switching=settings.phase_switch_enabled && phase_state!=PHASE_READY && phase_state!=PHASE_FAULT;
    bool enabled=evcc_control.enabled&&!evcc_control.expired&&!evcc_control.inhibited&&settings.enabled&&
        (target_locked(now)>0 || (switching && evcc_ready_locked(now)));
    UNLOCK();httpd_resp_set_type(req,"application/json");httpd_resp_set_hdr(req,"Cache-Control","no-store");
    return httpd_resp_sendstr(req,enabled?"true":"false");
}

static esp_err_t evcc_command_post(httpd_req_t *req) {
    if(!evcc_key_authorized(req))return ESP_OK;
    if(req->content_len>128)return httpd_resp_send_err(req,HTTPD_400_BAD_REQUEST,"evcc-Befehl zu gross.");
    cJSON *o=read_json(req);if(!o)return ESP_OK;
    bool current=!strcmp(req->uri,"/api/evcc/current");
    cJSON *value=cJSON_GetObjectItemCaseSensitive(o,current?"current_a":"enabled");
    bool shape=value && o->child==value && !value->next;
    LOCK();int64_t now=now_ms();bool ok=false;
    if(shape && current && cJSON_IsNumber(value))
        ok=evcc_set_current(&evcc_control,(float)value->valuedouble,active_min_a(),settings.max_charge_a,now);
    else if(shape && !current && cJSON_IsBool(value))
        ok=evcc_set_enabled(&evcc_control,cJSON_IsTrue(value),evcc_ready_locked(now),now);
    if(ok) {
        settings.manual_current_a=evcc_control.current_a;
        settings.manual_phases=evcc_control.phase_set?evcc_control.phases:settings.charge_phases;
        settings.mode=MODE_MANUAL;settings.enabled=evcc_control.enabled;
        if(!current)event_locked(evcc_control.enabled?"evcc_start":"evcc_stop");
    }
    UNLOCK();cJSON_Delete(o);
    if(!ok){httpd_resp_set_status(req,"409 Conflict");return httpd_resp_sendstr(req,"evcc-Befehl abgelehnt: Status, Messwerte, lokale Sperre oder Stromgrenzen pruefen.");}
    return ok_json(req);
}

static esp_err_t evcc_phases_get(httpd_req_t *req) {
    if(!evcc_key_authorized(req))return ESP_OK;
    LOCK();int64_t now=now_ms();evcc_touch(&evcc_control,now);
    bool ready=!settings.phase_switch_enabled || (phase_state==PHASE_READY && phase_feedback_matches(settings.phase_feedback_enabled,phase_observed(),settings.charge_phases));
    unsigned phases=settings.charge_phases;UNLOCK();
    if(!ready){httpd_resp_set_status(req,"503 Service Unavailable");httpd_resp_set_hdr(req,"Retry-After","5");return httpd_resp_sendstr(req,"Phasenwechsel noch nicht bestaetigt.");}
    httpd_resp_set_type(req,"application/json");httpd_resp_set_hdr(req,"Cache-Control","no-store");
    return httpd_resp_sendstr(req,phases==1?"1":"3");
}

static esp_err_t evcc_phases_post(httpd_req_t *req) {
    if(!evcc_key_authorized(req))return ESP_OK;
    if(req->content_len>64)return httpd_resp_send_err(req,HTTPD_400_BAD_REQUEST,"Phasenbefehl zu gross.");
    cJSON *o=read_json(req);if(!o)return ESP_OK;
    cJSON *value=cJSON_GetObjectItemCaseSensitive(o,"phases");
    bool shape=cJSON_IsNumber(value) && o->child==value && !value->next && (value->valuedouble==1 || value->valuedouble==3);
    LOCK();int64_t now=now_ms();unsigned phases=shape?(unsigned)value->valueint:0;
    bool supported=settings.phase_switch_enabled && phase_gpio_ready;
    bool same=evcc_control.phase_set && evcc_control.phases==phases;
    bool ready=evcc_ready_locked(now) && (phase_state==PHASE_READY || same);
    bool ok=shape && evcc_set_phases(&evcc_control,phases,supported,ready,now);
    if(ok){
        settings.manual_phases=(uint8_t)phases;
        if(!same){phase_idle_inhibit=false;phase_attempt_at=0;phase_no_load_since=0;event_locked("evcc_phases");}
    }
    bool complete=phase_state==PHASE_READY && settings.charge_phases==phases;
    UNLOCK();cJSON_Delete(o);
    if(!ok){httpd_resp_set_status(req,"409 Conflict");return httpd_resp_sendstr(req,"Phasenwechsel abgelehnt: Hardware, Messwerte, lokale Sperre oder laufenden Wechsel pruefen.");}
    if(!complete)httpd_resp_set_status(req,"202 Accepted");
    return ok_json(req);
}

static esp_err_t evcc_release_post(httpd_req_t *req) {
    if(!authorized(req))return ESP_OK;
    cJSON *o=read_json(req);if(!o)return ESP_OK;
    cJSON *value=cJSON_GetObjectItemCaseSensitive(o,"release");
    bool shape=cJSON_IsTrue(value) && o->child==value && !value->next;
    LOCK();bool ok=shape && evcc_local_release(&evcc_control,now_ms());
    if(ok){settings.enabled=false;settings.mode=MODE_OFF;event_locked("evcc_released");}
    UNLOCK();cJSON_Delete(o);
    if(!ok){httpd_resp_set_status(req,"409 Conflict");return httpd_resp_sendstr(req,"Keine lokale evcc-Sperre aktiv.");}
    return ok_json(req);
}
