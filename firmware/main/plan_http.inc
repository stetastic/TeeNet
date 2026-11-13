/* Use current commissioning evidence instead of the retired, invisible
   control_verified preference. Both energy and percentage plans share this. */
static const char *plan_start_problem_locked(int64_t now,bool clock_ok,bool manual) {
    if(settings.basic_mode||!settings.charge_plan_enabled)return "Ladeplan unter Zusatzfunktionen aktivieren.";
    if(!clock_ok)return "Uhrzeit noch nicht synchronisiert. WLAN und Internetzeit pruefen.";
    if(!manual&&!settings.zero_feed_enabled)return "Hausanschluss und PV-Regelung zuerst aktivieren.";
    if(settings.external_mode_input_enabled)return "Externen Moduskontakt fuer den Ladeplan deaktivieren.";
    if(restart_pending())return "Ladeplan: zuerst den ausstehenden Neustart durchfuehren.";
    if(restarting||ota_in_progress||phase_mqtt_saving||backup_in_progress||phase_test_active||phase_config_saving||boot_validation_pending)
        return "Wartung oder Schuetztest zuerst beenden.";
    if(boot_validation_failed||runtime_watchdog_failed)return "Interne Pruefung fehlgeschlagen. Diagnose pruefen.";
    if(!wallbox_ready||!fresh(now,wallbox_at,ems_param_ms(EP_SHELL_TTL)))return "Wallbox antwortet nicht ueber RS485.";
    if(!feedback_ready(now)||!fresh(now,meter_at,wallbox_meter_ttl())||!fresh(now,actual_at,wallbox_meter_ttl()))
        return "Frische Messwerte des Wallbox-Zaehlers fehlen.";
    if(!manual&&(!fresh(now,house_power_at,house_meter_ttl())||!isfinite(house_power_w)))return "Frische Messwerte des Hauszaehlers fehlen.";
    if(settings.phase_switch_enabled&&phase_state!=PHASE_READY)return "Phasenumschaltung ist noch nicht bereit.";
    return NULL;
}
static esp_err_t plan_handler(httpd_req_t *req) {
    LOCK();bool evcc_owner=evcc_control.configured||evcc_configuring;UNLOCK();
    if(evcc_owner){httpd_resp_set_status(req,"409 Conflict");return httpd_resp_sendstr(req,"Ladeplan im evcc-Modus nicht verfuegbar.");}
    if(!authorized(req))return ESP_OK;
    cJSON *o=read_json(req);if(!o)return ESP_OK;
    cJSON *active=cJSON_GetObjectItem(o,"active"),*kwh=cJSON_GetObjectItem(o,"kwh"),*deadline=cJSON_GetObjectItem(o,"deadline_epoch"),*resume=cJSON_GetObjectItem(o,"resume");
    cJSON *mode=cJSON_GetObjectItem(o,"mode"),*capacity=cJSON_GetObjectItem(o,"capacity_kwh"),*start_soc=cJSON_GetObjectItem(o,"current_soc"),*target_soc=cJSON_GetObjectItem(o,"target_soc");
    bool activate=cJSON_IsTrue(active),resuming=cJSON_IsTrue(resume),valid=cJSON_IsBool(active) && (!resume||cJSON_IsBool(resume));
    valid=valid&&(!mode||(cJSON_IsString(mode)&&(!strcmp(mode->valuestring,"manual")||!strcmp(mode->valuestring,"pv"))));
    for(cJSON *i=o->child;i;i=i->next)if(strcmp(i->string,"active")&&strcmp(i->string,"kwh")&&strcmp(i->string,"deadline_epoch")&&strcmp(i->string,"resume")&&strcmp(i->string,"mode")&&strcmp(i->string,"capacity_kwh")&&strcmp(i->string,"current_soc")&&strcmp(i->string,"target_soc"))valid=false;
    int64_t epoch,midnight;bool clock_ok=calendar(&epoch,&midnight)!=0;
    const char *problem=valid?NULL:"Ladeplan-Daten ungueltig. Seite neu laden.";
    LOCK();charge_plan_t next=charge_plan;soc_goal_t next_soc=soc_goal;bool vehicle_resume=activate&&resuming&&vehicle_plan.active;
    bool manual=mode?cJSON_IsString(mode)&&!strcmp(mode->valuestring,"manual"):resuming&&soc_goal.manual;
    bool percent=capacity||start_soc||target_soc;
    if(activate) {
        if(!problem)problem=plan_start_problem_locked(now_ms(),clock_ok,manual);
        float manual_a=resuming&&soc_goal.manual_current_a>0?soc_goal.manual_current_a:settings.manual_current_a;
        if(!problem&&manual&&(!isfinite(manual_a)||manual_a<active_min_a()||manual_a>settings.max_charge_a))problem="Gewuenschte Ladeleistung zuerst einstellen.";
        if(!problem&&resuming){
            if(mode&&manual!=soc_goal.manual)problem="Betriebsart zum Fortsetzen beibehalten oder einen neuen Ladeplan starten.";
            else if(vehicle_resume)problem="Fahrzeug-Ladeplan ueber das zugeordnete Fahrzeugprofil fortsetzen.";
            else if(!next.active||next.deadline_s<=epoch/1000)problem="Kein fortsetzbarer Ladeplan mit zukuenftiger Endzeit vorhanden.";
            else if(percent||kwh||deadline)problem="Zum Fortsetzen keine neuen Zielwerte senden.";
            else if(next_soc.active){float measured=plan_car_soc_locked(now_ms());if(isfinite(measured)&&measured>=next_soc.target_soc)problem="Das Prozentziel ist bereits erreicht.";else soc_goal_step(&next_soc,&next,true,measured);}
        }else if(!problem){
            if(!cJSON_IsNumber(deadline)||!isfinite(deadline->valuedouble)||floor(deadline->valuedouble)!=deadline->valuedouble||
                deadline->valuedouble<=epoch/1000||deadline->valuedouble>epoch/1000+7*86400)
                problem="Endzeit in der Zukunft innerhalb der naechsten 7 Tage waehlen.";
            else if(percent){
                float measured=plan_car_soc_locked(now_ms()),current=isfinite(measured)?measured:cJSON_IsNumber(start_soc)?(float)start_soc->valuedouble:NAN;
                if(kwh)problem="Entweder Akkustand oder kWh als Ladeziel senden.";
                else if(!cJSON_IsNumber(capacity)||!isfinite(capacity->valuedouble)||capacity->valuedouble<5||capacity->valuedouble>200)problem="Akkugroesse zwischen 5 und 200 kWh eingeben.";
                else if(!cJSON_IsNumber(start_soc)||!cJSON_IsNumber(target_soc)||!soc_goal_begin(&next_soc,&next,(float)capacity->valuedouble,current,(float)target_soc->valuedouble,(int64_t)deadline->valuedouble,epoch/1000,energy.store.total_wh[0],manual))problem="Gueltigen Ladestand und ein hoeheres Prozentziel bis 100 % eingeben.";
            }else if(!cJSON_IsNumber(kwh)||!isfinite(kwh->valuedouble)||kwh->valuedouble<.5||kwh->valuedouble>100)
                problem="Nachlademenge zwischen 0,5 und 100 kWh eingeben.";
            else if(!charge_plan_start(&next,kwh->valuedouble,(int64_t)deadline->valuedouble,epoch/1000,energy.store.total_wh[0]))
                problem="Energiezaehler ungueltig. Diagnose pruefen.";
            else{next.grid=manual;soc_goal_clear(&next_soc,manual);}
        }
    }else {next.active=false;next.complete=false;next.expired=false;soc_goal_clear(&next_soc,false);}
    valid=problem==NULL;
    if(valid) {
        restore_control_memory_locked(now_ms(),clock_ok?epoch:0,true);
        if(manual){if(resuming&&next_soc.manual_current_a>0){settings.manual_current_a=next_soc.manual_current_a;settings.manual_phases=next_soc.manual_phases;}else{next_soc.manual_current_a=settings.manual_current_a;next_soc.manual_phases=settings.manual_phases;}}
        vehicle_plan_clear_locked();soc_goal=next_soc;charge_plan=next;charge_plan.last_total_wh=energy.store.total_wh[0];plan_dirty=true;plan_revision++;
        settings.enabled=activate;settings.mode=activate?(next.grid?MODE_MANUAL:MODE_PV):MODE_OFF;
        if(next.grid&&activate&&!soc_goal.manual){settings.manual_phases=settings.phase_switch_enabled?3:settings.fixed_charge_phases;settings.manual_current_a=settings.max_charge_a;}
        plan_phase=activate?(next.grid?PLAN_GRID:PLAN_PV):PLAN_OFF;
        memset(&charge_guard,0,sizeof(charge_guard));
        pv_control_step(&pv_control,false,0,active_min_a(),settings.max_charge_a,now_ms());
        event_locked(activate?"plan_started":"plan_cancelled");
    }
    UNLOCK();cJSON_Delete(o);
    if(!valid)return httpd_resp_send_err(req,HTTPD_400_BAD_REQUEST,problem);
    if(!save_plan()){LOCK();settings.enabled=false;charge_plan.active=false;plan_phase=PLAN_OFF;plan_dirty=true;plan_revision++;event_locked("plan_storage");UNLOCK();
        return httpd_resp_send_err(req,HTTPD_500_INTERNAL_SERVER_ERROR,"Ladeplan konnte nicht gespeichert werden; Ladung gestoppt.");}
    return ok_json(req);
}
