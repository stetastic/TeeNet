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
static esp_err_t vehicle_plan_post(httpd_req_t *req){
    if(!authorized(req))return ESP_OK;
    cJSON *o=read_json(req);if(!o)return ESP_OK;
    const cJSON *slot=cJSON_GetObjectItem(o,"slot"),*start=cJSON_GetObjectItem(o,"current_soc"),*target=cJSON_GetObjectItem(o,"target_soc"),*deadline=cJSON_GetObjectItem(o,"deadline_epoch"),*resume=cJSON_GetObjectItem(o,"resume");
    const cJSON *mode=cJSON_GetObjectItem(o,"mode");bool manual=mode&&cJSON_IsString(mode)&&!strcmp(mode->valuestring,"manual");
    bool resuming=cJSON_IsTrue(resume);int index=cJSON_IsNumber(slot)&&slot->valueint>=1&&slot->valueint<=VEHICLE_COUNT?slot->valueint-1:-1;
    bool valid=index>=0&&index<VEHICLE_COUNT&&slot->valuedouble==slot->valueint&&cJSON_IsNumber(start)&&cJSON_IsNumber(target)&&cJSON_IsNumber(deadline)&&isfinite(deadline->valuedouble)&&floor(deadline->valuedouble)==deadline->valuedouble&&(!resume||cJSON_IsBool(resume))&&cJSON_GetArraySize(o)==4+(resume?1:0)+(mode?1:0)&&(!mode||(cJSON_IsString(mode)&&(!strcmp(mode->valuestring,"manual")||!strcmp(mode->valuestring,"pv"))));
    int64_t epoch,midnight;bool clock_ok=calendar(&epoch,&midnight)!=0;
    LOCK();int64_t now=now_ms();if(resuming&&!mode)manual=soc_goal.manual;
    const char *problem=valid?plan_start_problem_locked(now,clock_ok,manual):"Fahrzeug-Ladeplandaten ungueltig.";
    if(!problem&&resuming&&mode&&manual!=soc_goal.manual)problem="Betriebsart zum Fortsetzen beibehalten oder einen neuen Ladeplan starten.";
    valid=valid&&!problem&&vehicles.enabled&&!evcc_control.configured&&!evcc_configuring&&vehicle_http_fresh_locked(now)&&(settings.vehicle_fixed_profile||evcc_shell.session.present)&&(evcc_shell.state=='B'||evcc_shell.state=='C');
    float kwh=0,soc=0;
    if(valid){
        bool measured=vehicle_soc_fresh_locked(index,now);soc=measured?vehicle_soc_value_locked(index):(float)start->valuedouble;
        int current=vehicle_current_locked(now);
        valid=current==index&&
            vehicle_need(&vehicles.rows[index],soc,(float)target->valuedouble,&kwh)&&deadline->valuedouble>epoch/1000&&deadline->valuedouble<=epoch/1000+7*86400;
        if(resuming)valid=valid&&charge_plan.active&&vehicle_plan.active&&vehicle_plan.slot==index&&
            (settings.vehicle_fixed_profile||!vehicle_plan.card[0]||!strcmp(vehicle_plan.card,evcc_shell.session.card))&&
            (!vehicle_plan.identity[0]||!strcmp(vehicle_plan.identity,evcc_shell.session.identity));
        float maximum=vehicle_maximum_kw_locked(vehicles.rows[index].maximum_kw,charge_plan_maximum_kw(&settings,house_currents_ready_locked(now),evu_contact.stable));
        valid=valid&&maximum>0;
        if(valid){
            if(!resuming){
                vehicle_plan_clear_locked();soc_goal_clear(&soc_goal,manual);const vehicle_profile_t *p=&vehicles.rows[index];
                vehicle_plan=(vehicle_plan_t){.version=1,.active=true,.slot=index,.start_soc=soc,.target_soc=(float)target->valuedouble,.capacity_kwh=p->capacity_kwh,.efficiency=p->efficiency,.maximum_kw=p->maximum_kw,.last_soc=soc,.measured_soc=measured};
                snprintf(vehicle_plan.card,sizeof(vehicle_plan.card),"%s",settings.vehicle_fixed_profile?"":evcc_shell.session.card);snprintf(vehicle_plan.identity,sizeof(vehicle_plan.identity),"%s",evcc_shell.state=='C'?evcc_shell.session.identity:"");
                charge_plan=(charge_plan_t){.version=1,.active=true,.grid=manual,.target_wh=kwh*1000,.last_total_wh=energy.store.total_wh[0],.deadline_s=(int64_t)deadline->valuedouble};
            }
            {
                charge_plan.last_total_wh=energy.store.total_wh[0];vehicle_plan_reason[0]=0;vehicle_missing_since=0;
                restore_control_memory_locked(now,epoch,true);settings.enabled=true;settings.mode=charge_plan.grid?MODE_MANUAL:MODE_PV;
                if(soc_goal.manual&&resuming&&soc_goal.manual_current_a>0){settings.manual_current_a=soc_goal.manual_current_a;settings.manual_phases=soc_goal.manual_phases;}
                if(charge_plan.grid)vehicle_grid_current_locked(soc_goal.manual?fminf(maximum,settings.manual_current_a*ems_phase_voltage_sum(&settings,settings.manual_phases)/1000):maximum);
                if(soc_goal.manual){soc_goal.manual_current_a=settings.manual_current_a;soc_goal.manual_phases=settings.manual_phases;}
                plan_phase=charge_plan.grid?PLAN_GRID:PLAN_PV;plan_dirty=true;plan_revision++;memset(&charge_guard,0,sizeof(charge_guard));
                pv_control_step(&pv_control,false,0,active_min_a(),settings.max_charge_a,now);event_locked("plan_started");
            }
        }
    }
    UNLOCK();cJSON_Delete(o);
    if(!valid)return httpd_resp_send_err(req,HTTPD_400_BAD_REQUEST,problem?problem:"Fahrzeugprofil, aktuelle Shell-Daten und Ladeziel pruefen.");
    if(!save_plan()){LOCK();settings.enabled=false;charge_plan.active=false;vehicle_plan.active=false;plan_dirty=true;plan_revision++;UNLOCK();return httpd_resp_send_err(req,HTTPD_500_INTERNAL_SERVER_ERROR,"Plan nicht gespeichert; Ladung gestoppt.");}
    return send_json(req,vehicle_snapshot());
}
