typedef struct { const char *name; size_t offset,size; char type; } field_t;

#define FIELD(name,type) {#name,offsetof(settings_t,name),sizeof(((settings_t *)0)->name),type}

static const field_t fields[]={

    FIELD(shell_rs485_interface,'u'),FIELD(meter_rs485_interface,'u'),FIELD(house_rs485_interface,'u'),
    FIELD(shell_rs485_host,'s'),FIELD(meter_rs485_host,'s'),FIELD(house_rs485_host,'s'),FIELD(evcc_feature_enabled,'b'),
    FIELD(wifi_ssid,'s'),FIELD(wifi_password,'p'),FIELD(mqtt_uri,'s'),FIELD(mqtt_username,'s'),FIELD(mqtt_password,'p'),FIELD(mqtt_prefix,'s'),

    FIELD(grid_limit_a,'f'),FIELD(max_charge_a,'f'),FIELD(shell_limits_auto,'b'),FIELD(shell_setup_host,'s'),FIELD(xemex_address,'u'),FIELD(wallbox_tx_pin,'i'),FIELD(wallbox_rx_pin,'i'),


    FIELD(fixed_charge_phases,'u'),FIELD(nominal_v,'f'),FIELD(price_kwh,'f'),FIELD(solar_price_kwh,'f'),FIELD(control_verified,'b'),

    FIELD(zero_feed_enabled,'b'),FIELD(house_meter_type,'s'),FIELD(house_meter_host,'s'),FIELD(zero_reserve_w,'f'),FIELD(xemex_coils,'u'),FIELD(wallbox_meter_type,'s'),FIELD(meter_baud,'i'),FIELD(meter_format,'u'),FIELD(house_power_path,'s'),FIELD(mqtt_state_json,'b'),FIELD(battery_protect,'b'),FIELD(battery_reserve_soc,'f'),FIELD(battery_cloud_limit_w,'f'),FIELD(battery_assist_limit_w,'f'),FIELD(phase_switch_enabled,'b'),FIELD(phase_feedback_enabled,'b'),FIELD(phase_feedback_closed_is_single,'b'),
    FIELD(expert_mode,'b'),FIELD(basic_mode,'b'),FIELD(pv_allocation_enabled,'b'),FIELD(pv_priority,'u'),FIELD(pv_house_priority_w,'f'),FIELD(pv_car_priority_w,'f'),FIELD(control_status_visible,'b'),FIELD(vehicle_soc_enabled,'b'),FIELD(external_mode_input_enabled,'b'),FIELD(evu_input_enabled,'b'),
    FIELD(evu_limit_a,'f'),FIELD(grid_guard_enabled,'b'),
    FIELD(xemex_tx_pin,'i'),FIELD(xemex_rx_pin,'i'),FIELD(house_tx_pin,'i'),FIELD(house_rx_pin,'i'),
    FIELD(external_mode_input_pin,'i'),FIELD(evu_input_pin,'i'),FIELD(relay1_pin,'i'),
    FIELD(phase_feedback_pin,'i'),FIELD(phase_feedback_second_pin,'i'),FIELD(status_led_pin,'i'),
    FIELD(house_address,'u'),FIELD(house_meter_baud,'i'),FIELD(house_meter_format,'u'),
    FIELD(relay_board_enabled,'b'),FIELD(relay_active_low,'b'),
    FIELD(relay1_mode,'u'),
    FIELD(mqtt_enabled,'b'),FIELD(homeassistant_enabled,'b'),FIELD(pv_display_enabled,'b'),FIELD(mqtt_input_source,'u'),
    FIELD(heatpump_enabled,'b'),FIELD(heatpump_publish_enabled,'b'),FIELD(heatpump_source,'s'),
    FIELD(heatpump_host,'s'),FIELD(heatpump_input_topic,'s'),FIELD(heatpump_output_topic,'s'),
    FIELD(phase_actuator_type,'u'),FIELD(phase_shelly_host,'s'),FIELD(phase_shelly_password,'p'),
    FIELD(phase_feedback_count,'u'),FIELD(phase_shelly_model,'u'),FIELD(phase_shelly_pulse,'b'),FIELD(phase_shelly_true_three_phase,'b'),
    FIELD(wallbox_meter_host,'s'),FIELD(charge_plan_enabled,'b'),FIELD(huawei_enabled,'b'),FIELD(huawei_host,'s'),FIELD(huawei_unit_id,'u'),FIELD(huawei_battery,'b'),FIELD(huawei_pv,'b'),FIELD(opendtu_prefix,'s'),FIELD(opendtu_pv_topic,'s'),FIELD(opendtu_pv_valid_topic,'s'),FIELD(opendtu_current_positive_discharge,'b')};

/* Read-only status metadata must not turn a settings backup into an invalid
   restore request or bypass the separate phase-output authorization. */
static void config_remove_readonly(cJSON *config) {
    for(cJSON *item=config->child,*next;item;item=next){
        next=item->next;bool editable=false;
        for(size_t f=0;f<sizeof(fields)/sizeof(fields[0]);f++)
            if(!strcmp(item->string,fields[f].name)){editable=true;break;}
        if(!editable)cJSON_Delete(cJSON_DetachItemViaPointer(config,item));
    }
}
static esp_err_t config_apply(httpd_req_t *req,cJSON *o,bool restore) {
    if(!cJSON_IsObject(o)){
        cJSON_Delete(o);
        return httpd_resp_send_err(req,HTTPD_400_BAD_REQUEST,"Fehlerhafte Einstellungsdaten. Seite neu laden und erneut speichern.");
    }
    if(cJSON_HasObjectItem(o,"phase_mqtt_output_enabled")||cJSON_HasObjectItem(o,"phase_mqtt_topic")||
       cJSON_HasObjectItem(o,"phase_mqtt_true_three_phase")||cJSON_HasObjectItem(o,"phase_output_transport")||
       cJSON_HasObjectItem(o,"phase_output_host")||cJSON_HasObjectItem(o,"phase_output_switch_id")||
       cJSON_HasObjectItem(o,"phase_output_password")){
        cJSON_Delete(o);return httpd_resp_send_err(req,HTTPD_403_FORBIDDEN,"Ohne Rueckmeldung im Expertenmodus gesondert freigeben.");
    }
    for(unsigned role=2;role<EMS_GPIO_ROLES&&!restore;role++){
        cJSON *pin=cJSON_GetObjectItemCaseSensitive(o,gpio_roles[role].id);
        if(pin&&(!cJSON_IsNumber(pin)||pin->valuedouble!=settings_gpio_value(&saved_settings,role))){
            cJSON_Delete(o);return httpd_resp_send_err(req,HTTPD_403_FORBIDDEN,"GPIO-Belegung im Expertenmodus aendern.");
        }
    }
    cJSON *mqtt_profile=cJSON_GetObjectItemCaseSensitive(o,"phase_actuator_type");
    if(cJSON_IsNumber(mqtt_profile)&&mqtt_profile->valuedouble==2&&saved_settings.phase_actuator_type!=2){
        cJSON_Delete(o);return httpd_resp_send_err(req,HTTPD_403_FORBIDDEN,"MQTT-Schuetzsteuerung erfordert Warnbestaetigungen und erneutes Experten-Passwort.");
    }
    LOCK();bool maintenance=phase_test_active||boot_validation_pending||ota_in_progress||restarting||evcc_configuring||(evcc_control.configured&&evcc_control.enabled);UNLOCK();
    if(maintenance){cJSON_Delete(o);httpd_resp_set_status(req,"409 Conflict");return httpd_resp_sendstr(req,"Sicherung oder Update laeuft. Danach erneut versuchen.");}


    settings_t next; LOCK(); next=saved_settings; UNLOCK(); bool valid=true;
    char invalid_field[64]={0};

    for(cJSON *i=o->child;i && valid;i=i->next) {
        snprintf(invalid_field,sizeof(invalid_field),"%s",i->string?i->string:"unbekannt");
        if(!i->string){valid=false;break;}
        /* Reject ambiguous duplicate fields before applying their values. */
        for(cJSON *previous=o->child;previous!=i;previous=previous->next)
            if(previous->string&&!strcmp(previous->string,i->string)){valid=false;break;}
        if(!valid)break;

        const field_t *d=NULL;

        for(size_t f=0;f<sizeof(fields)/sizeof(fields[0]);f++) if(!strcmp(i->string,fields[f].name)) d=&fields[f];

        if(!d) { valid=false; break; }

        void *p=(char *)&next+d->offset;

        if(d->type=='s' || d->type=='p') { valid=cJSON_IsString(i) && strlen(i->valuestring)<d->size; if(valid) strcpy(p,i->valuestring); }

        else if(d->type=='b') { valid=cJSON_IsBool(i); if(valid) *(bool *)p=cJSON_IsTrue(i); }

        else {

            double v=i->valuedouble; valid=cJSON_IsNumber(i) && isfinite(v) && v>=-5000 && v<=38400;

            if(valid && d->type=='f') *(float *)p=v;

            else if(valid) {

                valid=floor(v)==v && (d->type=='i' ? (v>=INT_MIN && v<=INT_MAX) : (v>=0 && v<=255));

                if(valid && d->type=='i') *(int *)p=(int)v;

                if(valid && d->type=='u') *(uint8_t *)p=(uint8_t)v;

            }

        }

    }

    cJSON_Delete(o);
    /* A partial request must never be validated as if it were a complete
       configuration: later host/coils may not have been parsed at all. */
    if(!valid){
        char message[192];
        snprintf(message,sizeof(message),"Fehlerhafte Einstellungsdaten (%s). Seite neu laden und erneut speichern.",invalid_field);
        return httpd_resp_send_err(req,HTTPD_400_BAD_REQUEST,message);
    }
    /* These legacy values are intentionally no longer configurable. */
    next.wallbox_rts_pin=-1;
    next.xemex_rts_pin=-1;
    next.house_rts_pin=-1;
    next.power_factor=1;
    next.wallbox_address=1;next.house_xemex_coils=3;
    settings_fixed_pins(&next);
    settings_basic_mode(&next);
    if(!heatpump_settings_valid(&next))
        return httpd_resp_send_err(req,HTTPD_400_BAD_REQUEST,"Waermepumpe: Shelly-Adresse oder MQTT-Broker/Topics pruefen. Eingang und Ausgang getrennt halten.");
    if(!settings_shell_pin_available(&next,next.wallbox_tx_pin,true) ||
       !settings_shell_pin_available(&next,next.wallbox_rx_pin,false))
        return httpd_resp_send_err(req,HTTPD_400_BAD_REQUEST,"Shell-Pins: freie, geeignete GPIOs fuer TX und RX waehlen; keine Doppelbelegung.");
    /* The legacy combined /state topic was removed from the user interface. */
    next.mqtt_state_json=false;
    if(!next.relay_board_enabled) next.phase_switch_enabled=false;
    /* Home Assistant discovery follows MQTT automatically. The former switch
       was misleading because retained entities may remain visible in HA. */
    next.homeassistant_enabled=next.mqtt_enabled;
    if(next.mqtt_input_source==1)next.mqtt_input_source=0;
    if(!next.mqtt_enabled){next.vehicle_soc_enabled=false;
        if(!strcmp(next.heatpump_source,"mqtt"))next.heatpump_enabled=false;
        if(!(next.huawei_enabled && next.huawei_pv))next.pv_display_enabled=false;
        if(!(next.huawei_enabled && next.huawei_battery))next.battery_protect=false;}
    if(next.phase_actuator_type!=2)next.phase_feedback_enabled=true;
    next.relay1_mode=next.phase_switch_enabled?3:0;
    if(next.phase_actuator_type==2)next.phase_feedback_enabled=false;
    if(next.phase_actuator_type!=2||!next.phase_switch_enabled||!next.relay_board_enabled||
       (next.phase_output_transport==0&&!next.mqtt_enabled)){
        next.phase_mqtt_output_enabled=false;
        /* Ordinary settings may remove a prerequisite of the protected
           no-feedback profile. Return to the normal feedback profile instead
           of leaving an impossible actuator configuration behind. */
        if(next.phase_actuator_type==2){next.phase_actuator_type=0;next.phase_feedback_enabled=true;}
    }
    next.enabled=false; next.mode=MODE_OFF; next.pv_surplus_a=0;
    if(restore)next.control_verified=false;
    /* Clear the retired Solis experiment while retaining its settings-byte slot. */
    next.reserved_solis_discharge=false;
    next.reserved_http_meter_enabled=false;next.reserved_http_meter_host[0]=0;next.reserved_feedback_source=0;
    next.house_meter_password[0]=0;next.wallbox_meter_password[0]=0;

    /* A different physical meter invalidates the previous commissioning check. */

    bool meter_changed=next.shell_rs485_interface!=saved_settings.shell_rs485_interface ||
        next.meter_rs485_interface!=saved_settings.meter_rs485_interface ||
        strcmp(next.shell_rs485_host,saved_settings.shell_rs485_host) ||
        strcmp(next.meter_rs485_host,saved_settings.meter_rs485_host) || strcmp(next.wallbox_meter_type,saved_settings.wallbox_meter_type) ||
        strcmp(next.wallbox_meter_host,saved_settings.wallbox_meter_host) ||
        next.xemex_coils!=saved_settings.xemex_coils || next.meter_baud!=saved_settings.meter_baud ||
        next.meter_format!=saved_settings.meter_format || next.xemex_address!=saved_settings.xemex_address ||
        next.wallbox_tx_pin!=saved_settings.wallbox_tx_pin || next.wallbox_rx_pin!=saved_settings.wallbox_rx_pin ||
        next.xemex_tx_pin!=saved_settings.xemex_tx_pin || next.xemex_rx_pin!=saved_settings.xemex_rx_pin;

    if(meter_changed) { next.control_verified=false;next.current_offset_a=.8f; }

    next.manual_current_a=fminf(next.manual_current_a,next.max_charge_a);

    LOCK(); bool phase_change_allowed=phase_setup_change_allowed(
        saved_settings.phase_switch_enabled,next.phase_switch_enabled,
        settings.charge_phases,phase_config_quiescent_locked(),
        settings.enabled||(evcc_control.configured&&evcc_control.enabled&&!evcc_control.inhibited),
        actual_at>0||meter_at>0,
        fresh(now_ms(),actual_at,wallbox_meter_ttl()),actual_a); UNLOCK();
    LOCK();bool fixed_change_allowed=fixed_wiring_change_allowed(
        saved_settings.fixed_charge_phases,next.fixed_charge_phases,
        settings.enabled||(evcc_control.configured&&evcc_control.enabled&&!evcc_control.inhibited),
        phase_config_quiescent_locked(),actual_at>0||meter_at>0,
        fresh(now_ms(),actual_at,wallbox_meter_ttl()),actual_a);UNLOCK();
    LOCK();bool actuator_change=next.phase_actuator_type!=saved_settings.phase_actuator_type ||
        next.relay_active_low!=saved_settings.relay_active_low ||
        strcmp(next.phase_shelly_host,saved_settings.phase_shelly_host) ||
        next.phase_shelly_model!=saved_settings.phase_shelly_model||next.phase_shelly_pulse!=saved_settings.phase_shelly_pulse||
        next.phase_shelly_true_three_phase!=saved_settings.phase_shelly_true_three_phase||
        strcmp(next.phase_shelly_password,saved_settings.phase_shelly_password);
    config_gate_t hardware_gate=phase_config_gate_locked(now_ms());
    bool hardware_change_allowed=hardware_gate==CONFIG_GATE_OK;
    bool actuator_change_allowed=!actuator_change||hardware_change_allowed;UNLOCK();
    if(!actuator_change_allowed)return httpd_resp_send_err(req,HTTPD_400_BAD_REQUEST,phase_config_gate_message(hardware_gate));
    LOCK();bool feedback_changed=next.phase_feedback_enabled!=saved_settings.phase_feedback_enabled ||
        next.phase_feedback_count!=saved_settings.phase_feedback_count||
        next.phase_feedback_closed_is_single!=saved_settings.phase_feedback_closed_is_single;
    bool feedback_change_allowed=!feedback_changed||hardware_change_allowed;
    bool phase_configuration_changed=actuator_change||feedback_changed||
        next.phase_switch_enabled!=saved_settings.phase_switch_enabled||
        next.fixed_charge_phases!=saved_settings.fixed_charge_phases;
    UNLOCK();
    if(!feedback_change_allowed)return httpd_resp_send_err(req,HTTPD_400_BAD_REQUEST,phase_config_gate_message(hardware_gate));
    if(!fixed_change_allowed)return httpd_resp_send_err(req,HTTPD_400_BAD_REQUEST,phase_config_gate_message(hardware_gate));
    if(!phase_change_allowed)return httpd_resp_send_err(req,HTTPD_400_BAD_REQUEST,phase_config_gate_message(hardware_gate));
    char house_url[256];

    bool next_tasmota_house=!strcmp(next.house_meter_type,"tasmota");
    bool next_three_phase_house=(!strcmp(next.house_meter_type,"xemex") && next.house_xemex_coils==3) ||
        !strcmp(next.house_meter_type,"sdm630") || !strcmp(next.house_meter_type,"sdm630mct") ||
        !strcmp(next.house_meter_type,"shelly_gen2") || !strcmp(next.house_meter_type,"em24_tcp") ||
        (!strcmp(next.house_meter_type,"huawei") && next.huawei_enabled);
    if(next.battery_protect && (!(next.mqtt_enabled || (next.huawei_enabled && next.huawei_battery)) || !next.zero_feed_enabled || next.zero_reserve_w>0))
        return httpd_resp_send_err(req,HTTPD_400_BAD_REQUEST,
            "Hausakku benoetigt eine Datenquelle, einen PV-Hauszaehler und ein Netzziel von hoechstens 0 W.");
    if(next.phase_switch_enabled && (!next.relay_board_enabled || next.relay1_mode!=3))
        return httpd_resp_send_err(req,HTTPD_400_BAD_REQUEST,
            "Phasenumschaltung ist nicht vollstaendig eingerichtet. Verbindung und Schuetzoption pruefen.");
    if((next.phase_actuator_type==1||next.phase_actuator_type==3)&&next.phase_switch_enabled){
        if(!next.phase_shelly_host[0])
            return httpd_resp_send_err(req,HTTPD_400_BAD_REQUEST,"Shelly-Adresse fehlt. Unter Shelly-Schaltgeraet eintragen.");
        if(!phase_switch_meter_supported(&next)){
            char message[224];
            if(!strcmp(next.wallbox_meter_type,"xemex"))
                snprintf(message,sizeof(message),"Wallbox-Zaehler: Xemex mit %u Messspule(n) eingestellt. Fuer die Shelly-Phasenumschaltung mindestens L1 und L2 messen.",(unsigned)next.xemex_coils);
            else
                snprintf(message,sizeof(message),"Wallbox-Zaehlerprofil '%s' erfasst nicht mindestens L1 und L2. Fuer die Shelly-Phasenumschaltung ein passendes Profil waehlen.",next.wallbox_meter_type);
            return httpd_resp_send_err(req,HTTPD_400_BAD_REQUEST,message);
        }
    }
    if(next.grid_guard_enabled && !next_three_phase_house)
        return httpd_resp_send_err(req,HTTPD_400_BAD_REQUEST,
            "Hausanschlussschutz benoetigt einen Zaehler mit drei gemessenen Phasenstroemen.");
    if(!settings_valid(&next) || (next.zero_feed_enabled && next_tasmota_house &&
       !house_query_url(next.house_meter_type,next.house_meter_host,next.house_power_path,house_url,sizeof(house_url))))
        return httpd_resp_send_err(req,HTTPD_400_BAD_REQUEST,"Einstellungen ungueltig. Pins, Zaehler, Ladegrenzen und WLAN-Passwort pruefen.");

    /* Recheck under lock and reserve the controller before flash I/O. A
       worker may have started since validation; never race its command. */
    LOCK();hardware_gate=phase_config_gate_locked(now_ms());
    if(phase_configuration_changed&&hardware_gate!=CONFIG_GATE_OK){UNLOCK();return httpd_resp_send_err(req,HTTPD_400_BAD_REQUEST,phase_config_gate_message(hardware_gate));}
    phase_config_saving=phase_configuration_changed;UNLOCK();
    if(save_config(&next)!=ESP_OK){
        LOCK();phase_config_saving=false;UNLOCK();
        return httpd_resp_send_err(req,HTTPD_500_INTERNAL_SERVER_ERROR,"Speichern fehlgeschlagen.");
    }
    phase_config_saving=false; prepared_settings=next; prepared_count++; req->status=200; return ESP_OK;
}
