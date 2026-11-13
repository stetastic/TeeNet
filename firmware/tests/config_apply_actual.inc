typedef struct { const char *name; size_t offset,size; char type; } field_t;

#define FIELD(name,type) {#name,offsetof(settings_t,name),sizeof(((settings_t *)0)->name),type}

static const field_t fields[]={

    FIELD(shell_rs485_interface,'u'),FIELD(meter_rs485_interface,'u'),FIELD(house_rs485_interface,'u'),
    FIELD(shell_rs485_host,'s'),FIELD(meter_rs485_host,'s'),FIELD(house_rs485_host,'s'),FIELD(evcc_feature_enabled,'b'),
    FIELD(wifi_ssid,'s'),FIELD(wifi_password,'p'),FIELD(mqtt_uri,'s'),FIELD(mqtt_username,'s'),FIELD(mqtt_password,'p'),FIELD(mqtt_prefix,'s'),

    FIELD(shell_http_control_enabled,'b'),FIELD(grid_limit_a,'f'),FIELD(max_charge_a,'f'),FIELD(shell_limits_auto,'b'),FIELD(shell_setup_host,'s'),FIELD(xemex_address,'u'),FIELD(wallbox_tx_pin,'i'),FIELD(wallbox_rx_pin,'i'),


    FIELD(fixed_charge_phases,'u'),FIELD(nominal_v,'f'),FIELD(price_kwh,'f'),FIELD(solar_price_kwh,'f'),FIELD(control_verified,'b'),

    FIELD(zero_feed_enabled,'b'),FIELD(house_meter_type,'s'),FIELD(house_meter_host,'s'),FIELD(zero_reserve_w,'f'),FIELD(xemex_coils,'u'),FIELD(wallbox_meter_type,'s'),FIELD(meter_baud,'i'),FIELD(meter_format,'u'),FIELD(house_power_path,'s'),FIELD(mqtt_state_json,'b'),FIELD(battery_protect,'b'),FIELD(battery_reserve_soc,'f'),FIELD(battery_cloud_limit_w,'f'),FIELD(battery_assist_limit_w,'f'),FIELD(phase_switch_enabled,'b'),FIELD(phase_feedback_source,'u'),FIELD(phase_voltage_protocol,'u'),FIELD(phase_voltage_host,'s'),
    FIELD(expert_mode,'b'),FIELD(basic_mode,'b'),FIELD(pv_allocation_enabled,'b'),FIELD(pv_priority,'u'),FIELD(pv_house_priority_w,'f'),FIELD(pv_car_priority_w,'f'),FIELD(control_status_visible,'b'),FIELD(vehicle_soc_enabled,'b'),FIELD(external_mode_input_enabled,'b'),FIELD(evu_input_enabled,'b'),
    FIELD(evu_limit_a,'f'),FIELD(grid_guard_enabled,'b'),
    FIELD(xemex_tx_pin,'i'),FIELD(xemex_rx_pin,'i'),FIELD(house_tx_pin,'i'),FIELD(house_rx_pin,'i'),
    FIELD(external_mode_input_pin,'i'),FIELD(evu_input_pin,'i'),FIELD(relay1_pin,'i'),
    FIELD(status_led_pin,'i'),
    FIELD(house_address,'u'),FIELD(house_meter_baud,'i'),FIELD(house_meter_format,'u'),
    FIELD(relay_board_enabled,'b'),FIELD(relay_active_low,'b'),
    FIELD(relay1_mode,'u'),
    FIELD(mqtt_enabled,'b'),FIELD(homeassistant_enabled,'b'),FIELD(pv_display_enabled,'b'),FIELD(mqtt_input_source,'u'),
    FIELD(heatpump_enabled,'b'),FIELD(heatpump_publish_enabled,'b'),FIELD(heatpump_source,'s'),
    FIELD(heatpump_host,'s'),FIELD(heatpump_input_topic,'s'),FIELD(heatpump_output_topic,'s'),
    FIELD(phase_mqtt_topic,'s'),FIELD(phase_mqtt_true_three_phase,'b'),FIELD(phase_actuator_type,'u'),FIELD(phase_shelly_host,'s'),FIELD(phase_shelly_password,'p'),
    FIELD(phase_shelly_model,'u'),FIELD(phase_shelly_true_three_phase,'b'),
    FIELD(wallbox_shelly_reverse,'b'),FIELD(vehicle_fixed_profile,'u'),FIELD(vehicle_manual_soc_prompt,'b'),FIELD(sungrow_enabled,'b'),FIELD(sungrow_pv,'b'),FIELD(sungrow_battery,'b'),FIELD(sungrow_profile,'u'),FIELD(sungrow_unit_id,'u'),FIELD(sungrow_port,'i'),FIELD(sungrow_host,'s'),FIELD(sungrow_model,'s'),
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
static esp_err_t config_field_error(httpd_req_t *req,const char *field,const char *message){
    cJSON *o=cJSON_CreateObject();
    if(!o||!cJSON_AddStringToObject(o,"error",message)||!cJSON_AddStringToObject(o,"field",field)){cJSON_Delete(o);return httpd_resp_send_err(req,HTTPD_500_INTERNAL_SERVER_ERROR,"Out of memory");}
    httpd_resp_set_status(req,"400 Bad Request");return send_json(req,o);
}
static esp_err_t config_apply(httpd_req_t *req,cJSON *o,bool restore) {
    if(!cJSON_IsObject(o)){
        cJSON_Delete(o);
        return httpd_resp_send_err(req,HTTPD_400_BAD_REQUEST,"Fehlerhafte Einstellungsdaten. Seite neu laden und erneut speichern.");
    }
#if CONFIG_TEE_HTTP_CONTROL_TRIAL
    LOCK();bool offset_testing=normal_offset_trial.armed||shell_http_trial.armed;UNLOCK();
    if(offset_testing){cJSON_Delete(o);httpd_resp_set_status(req,"409 Conflict");return httpd_resp_sendstr(req,"Reglertest zuerst stoppen und beenden.");}
#endif
    if(cJSON_HasObjectItem(o,"phase_mqtt_output_enabled")||cJSON_HasObjectItem(o,"phase_output_transport")||
       cJSON_HasObjectItem(o,"phase_output_host")||cJSON_HasObjectItem(o,"phase_output_switch_id")||
       cJSON_HasObjectItem(o,"phase_output_password")){
        cJSON_Delete(o);return httpd_resp_send_err(req,HTTPD_403_FORBIDDEN,"Schuetzsteuerung unter Phasenumschaltung einrichten.");
    }
    for(unsigned role=2;role<EMS_GPIO_ROLES&&!restore;role++){
        cJSON *pin=cJSON_GetObjectItemCaseSensitive(o,gpio_roles[role].id);
        if(pin&&(!cJSON_IsNumber(pin)||pin->valuedouble!=settings_gpio_value(&saved_settings,role))){
            cJSON_Delete(o);return httpd_resp_send_err(req,HTTPD_403_FORBIDDEN,"GPIO-Belegung im Expertenmodus aendern.");
        }
    }
    LOCK();bool maintenance=phase_test_active||boot_validation_pending||ota_in_progress||restarting||evcc_configuring||(evcc_control.configured&&evcc_control.enabled);UNLOCK();
    if(maintenance){cJSON_Delete(o);httpd_resp_set_status(req,"409 Conflict");return httpd_resp_sendstr(req,"Sicherung oder Update laeuft. Danach erneut versuchen.");}


    settings_t next; LOCK(); next=saved_settings; UNLOCK(); bool valid=true;
    bool restore_http_present=cJSON_HasObjectItem(o,"shell_http_control_enabled");
    char invalid_field[64]={0};

    for(cJSON *i=o->child;i && valid;i=i->next) {
        snprintf(invalid_field,sizeof(invalid_field),"%s",i->string?i->string:"unbekannt");
        if(!i->string){valid=false;break;}
        /* Reject ambiguous duplicate fields before applying their values. */
        for(cJSON *previous=o->child;previous!=i;previous=previous->next)
            if(previous->string&&!strcmp(previous->string,i->string)){valid=false;break;}
        if(!valid)break;


        if(!strcmp(i->string,"phase_shelly_pulse")){
            if(!cJSON_IsBool(i)){cJSON_Delete(o);return config_field_error(req,"phase_actuator_type","Schuetztyp ungueltig.");}
            if(cJSON_IsTrue(i)){
                if(!restore){cJSON_Delete(o);return config_field_error(req,"phase_actuator_type","Bistabile Impulsansteuerung entfallen. Dauerhaftes Schuetz, z. B. Hager ESC226, einrichten.");}
                next.phase_contactor_reconfigure=true;
            }
            continue;
        }
        /* Old backups/forms may contain retired contact settings. Ignore their
           storage bytes; they cannot allocate a GPIO or enable a contact check. */
        if(!strcmp(i->string,"phase_feedback_pin")||!strcmp(i->string,"phase_feedback_second_pin")||
           !strcmp(i->string,"phase_feedback_count")||!strcmp(i->string,"phase_feedback_closed_is_single")||
           !strcmp(i->string,"phase_feedback_enabled")||!strcmp(i->string,"phase_no_feedback_acknowledged"))continue;
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
        return config_field_error(req,invalid_field,message);
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
    if(!settings_shell_pin_available(&next,next.wallbox_tx_pin,true))
        return config_field_error(req,"wallbox_tx_pin","Shell TX-Pin: freien, geeigneten GPIO ohne Doppelbelegung waehlen.");
    if(!settings_shell_pin_available(&next,next.wallbox_rx_pin,false))
        return config_field_error(req,"wallbox_rx_pin","Shell RX-Pin: freien, geeigneten GPIO ohne Doppelbelegung waehlen.");
    /* The legacy combined /state topic was removed from the user interface. */
    next.mqtt_state_json=false;
    if(!next.relay_board_enabled) next.phase_switch_enabled=false;
    /* Home Assistant discovery follows MQTT automatically. The former switch
       was misleading because retained entities may remain visible in HA. */
    next.homeassistant_enabled=next.mqtt_enabled;
    if(next.mqtt_input_source==1)next.mqtt_input_source=0;
    if(!next.mqtt_enabled){next.vehicle_soc_enabled=false;
        if(!strcmp(next.heatpump_source,"mqtt"))next.heatpump_enabled=false;
        if(!(next.huawei_enabled && next.huawei_pv)&&!(next.sungrow_enabled && next.sungrow_pv))next.pv_display_enabled=false;
        if(!(next.huawei_enabled && next.huawei_battery)&&!(next.sungrow_enabled && next.sungrow_battery))next.battery_protect=false;}
    if(next.sungrow_enabled&&!next.sungrow_host[0])return config_field_error(req,"sungrow_host","IP-Adresse oder Hostname des Sungrow eintragen.");
    if(!strcmp(next.house_meter_type,"sungrow")&&(!next.sungrow_enabled||next.sungrow_profile==1))return config_field_error(req,"house_meter_type","Dieses Sungrow-Profil liefert keine Hausanschlussmessung. Separaten Hauszaehler waehlen.");
    if(next.sungrow_enabled&&next.huawei_enabled&&((next.sungrow_pv&&next.huawei_pv)||(next.sungrow_battery&&next.huawei_battery)))return config_field_error(req,"sungrow_enabled","Je Messwert nur Huawei oder Sungrow als Quelle aktivieren.");
    LOCK();
    bool vehicle_selection_changed=next.vehicle_fixed_profile!=saved_settings.vehicle_fixed_profile;
    bool vehicle_selection_valid=next.vehicle_fixed_profile==0||(next.vehicle_fixed_profile<=VEHICLE_COUNT&&vehicles.rows[next.vehicle_fixed_profile-1].name[0]);
    bool vehicle_selection_busy=vehicle_selection_changed&&vehicle_plan.active&&charge_plan.active;
    UNLOCK();
    if(vehicle_selection_changed&&!vehicle_selection_valid)return config_field_error(req,"vehicle_fixed_profile","Zuerst das gewuenschte Fahrzeugprofil speichern.");
    if(vehicle_selection_busy)return config_field_error(req,"vehicle_fixed_profile","Fahrzeug-Ladeplan vor dem Profilwechsel beenden.");
    if(restore&&next.phase_contactor_reconfigure)next.phase_switch_enabled=false;
    if(next.phase_switch_enabled)next.phase_contactor_reconfigure=false;
    next.phase_feedback_enabled=phase_uses_voltage(&next);
    if(next.phase_switch_enabled&&phase_uses_voltage(&next)&&!shelly_phase_endpoint_valid(next.phase_voltage_host,sizeof(next.phase_voltage_host)))
        return config_field_error(req,"phase_voltage_host","Adresse des Shelly 3EM hinter dem Schuetz eintragen.");
    if(next.phase_actuator_type==2&&next.phase_switch_enabled){next.phase_mqtt_output_enabled=true;next.phase_output_transport=0;}

    next.relay1_mode=next.phase_switch_enabled?3:0;

    if(next.phase_actuator_type!=2||!next.phase_switch_enabled||!next.relay_board_enabled||
       (next.phase_output_transport==0&&!next.mqtt_enabled)){
        next.phase_mqtt_output_enabled=false;
        /* Ordinary settings may remove a prerequisite of the protected
           no-feedback profile. Return to the normal feedback profile instead
           of leaving an impossible actuator configuration behind. */
        if(next.phase_actuator_type==2)next.phase_actuator_type=0;
    }
    next.enabled=false; next.mode=MODE_OFF; next.pv_surplus_a=0;
    if(restore)next.control_verified=false;
    /* Clear the retired Solis experiment while retaining its settings-byte slot. */
    next.reserved_solis_discharge=false;
    if(restore&&!restore_http_present)next.shell_http_control_enabled=false;
    LOCK();bool http_conflict=next.shell_http_control_enabled&&evcc_control.configured;UNLOCK();
    if(http_conflict)return config_field_error(req,"shell_http_control_enabled","evcc zuerst deaktivieren.");
    if(next.shell_http_control_enabled&&!next.shell_setup_host[0])return config_field_error(req,"shell_setup_host","IP-Adresse der Shell-Wallbox eintragen.");
    next.reserved_http_meter_enabled=false;next.reserved_http_meter_host[0]=0;next.reserved_feedback_source=0;
    next.house_meter_password[0]=0;next.wallbox_meter_password[0]=0;

    /* A different physical meter invalidates the previous commissioning check. */

    bool meter_changed=next.wallbox_shelly_reverse!=saved_settings.wallbox_shelly_reverse || next.shell_rs485_interface!=saved_settings.shell_rs485_interface ||
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
        next.phase_mqtt_true_three_phase!=saved_settings.phase_mqtt_true_three_phase||strcmp(next.phase_mqtt_topic,saved_settings.phase_mqtt_topic)||
        strcmp(next.phase_shelly_host,saved_settings.phase_shelly_host) ||
        next.phase_shelly_model!=saved_settings.phase_shelly_model||
        next.phase_shelly_true_three_phase!=saved_settings.phase_shelly_true_three_phase||
        strcmp(next.phase_shelly_password,saved_settings.phase_shelly_password);
    config_gate_t hardware_gate=phase_config_gate_locked(now_ms());
    bool hardware_change_allowed=hardware_gate==CONFIG_GATE_OK;
    bool actuator_change_allowed=!actuator_change||hardware_change_allowed;UNLOCK();
    if(!actuator_change_allowed)return httpd_resp_send_err(req,HTTPD_400_BAD_REQUEST,phase_config_gate_message(hardware_gate));
    LOCK();bool feedback_changed=next.wallbox_shelly_reverse!=saved_settings.wallbox_shelly_reverse ||
        next.phase_feedback_source!=saved_settings.phase_feedback_source||next.phase_voltage_protocol!=saved_settings.phase_voltage_protocol||
        strcmp(next.phase_voltage_host,saved_settings.phase_voltage_host);
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
    if(next.battery_protect && (!(next.mqtt_enabled || (next.huawei_enabled && next.huawei_battery) || (next.sungrow_enabled && next.sungrow_battery)) || !next.zero_feed_enabled || next.zero_reserve_w>0))
        return httpd_resp_send_err(req,HTTPD_400_BAD_REQUEST,
            "Hausakku benoetigt eine Datenquelle, einen PV-Hauszaehler und ein Netzziel von hoechstens 0 W.");
    if(next.phase_switch_enabled && (!next.relay_board_enabled || next.relay1_mode!=3))
        return httpd_resp_send_err(req,HTTPD_400_BAD_REQUEST,
            "Phasenumschaltung ist nicht vollstaendig eingerichtet. Verbindung und Schuetzoption pruefen.");
    if((next.phase_actuator_type==1||next.phase_actuator_type==3)&&next.phase_switch_enabled){
        if(!next.phase_shelly_host[0])
            return config_field_error(req,"phase_shelly_host","Shelly-Adresse fehlt. Unter Shelly-Schaltgeraet eintragen.");
        if(!phase_switch_meter_supported(&next)){
            char message[224];
            if(!strcmp(next.wallbox_meter_type,"xemex"))
                snprintf(message,sizeof(message),"Wallbox-Zaehler: Xemex mit %u Messspule(n). Fuer die Phasenumschaltung mindestens L1 und L2 messen.",(unsigned)next.xemex_coils);
            else
                snprintf(message,sizeof(message),"Wallbox-Zaehler: Profil '%s' misst nicht L1 und L2. Passendes Zaehlerprofil waehlen.",next.wallbox_meter_type);
            return config_field_error(req,!strcmp(next.wallbox_meter_type,"xemex")?"xemex_coils":"wallbox_meter_type",message);
        }
    }
    if(next.grid_guard_enabled && !next_three_phase_house)
        return httpd_resp_send_err(req,HTTPD_400_BAD_REQUEST,
            "Hausanschlussschutz benoetigt einen Zaehler mit drei gemessenen Phasenstroemen.");
    bool network_wallbox=!strcmp(next.wallbox_meter_type,"shelly_gen2")||!strcmp(next.wallbox_meter_type,"shelly_em1")||!strcmp(next.wallbox_meter_type,"em24_tcp");
    bool meter_host_required=network_wallbox&&!next.shell_http_control_enabled;
#if defined(CONFIG_TEE_BOARD_ATOMS3_LITE) && CONFIG_TEE_BOARD_ATOMS3_LITE
    meter_host_required=meter_host_required&&(next.control_verified||next.enabled);
#endif
    if(meter_host_required&&!next.wallbox_meter_host[0])
        return config_field_error(req,"wallbox_meter_host","Wallbox-Zaehler: Geraet auswaehlen oder IP-Adresse eintragen.");
    if(network_wallbox)for(const unsigned char *p=(const unsigned char *)next.wallbox_meter_host;*p;p++)if(!isalnum(*p)&&*p!='.'&&*p!='-'&&*p!=':')
        return config_field_error(req,"wallbox_meter_host","Wallbox-Zaehler: Nur IP-Adresse oder Hostname, ohne http:// und Pfad eintragen.");
    if(next.wifi_password[0]&&(strlen(next.wifi_password)<8||strlen(next.wifi_password)>63))
        return config_field_error(req,"wifi_password","WLAN-Passwort muss 8 bis 63 Zeichen lang sein.");
    if(next.max_charge_a<EMS_MIN_CHARGE_A||next.max_charge_a>63||next.max_charge_a>next.grid_limit_a||next.max_charge_a<next.min_charge_a)
        return config_field_error(req,"max_charge_a","Maximalstrom muss zur Ladeuntergrenze und zum Hausanschluss passen.");
    if(next.shell_rs485_interface==1&&!rs485_endpoint_valid(next.shell_rs485_host,sizeof(next.shell_rs485_host)))
        return config_field_error(req,"shell_rs485_host","IP-Adresse und Port des Shell-RS485-Gateways pruefen.");
    if(!network_wallbox&&next.meter_rs485_interface==1&&!rs485_endpoint_valid(next.meter_rs485_host,sizeof(next.meter_rs485_host)))
        return config_field_error(req,"meter_rs485_host","IP-Adresse und Port des Zaehler-RS485-Gateways pruefen.");
    bool house_network=!strcmp(next.house_meter_type,"tasmota")||!strcmp(next.house_meter_type,"shelly_gen2")||!strcmp(next.house_meter_type,"shelly_em1")||!strcmp(next.house_meter_type,"em24_tcp");
    bool house_full_url=!strncmp(next.house_power_path,"http://",7)||!strncmp(next.house_power_path,"https://",8);
    if(next.zero_feed_enabled&&house_network&&!next.house_meter_host[0]&&!house_full_url)
        return config_field_error(req,"house_meter_host","Hauszaehler auswaehlen oder seine IP-Adresse eintragen.");
    if(next.zero_feed_enabled&&next_tasmota_house&&!house_query_url(next.house_meter_type,next.house_meter_host,next.house_power_path,house_url,sizeof(house_url)))
        return config_field_error(req,"house_power_path","Adresse und Abfragepfad des Hauszaehlers pruefen.");
    if(!settings_valid(&next))
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
    if(restore&&!vehicle_restore_commit()){
        bool rolled_back=save_config(&saved_settings)==ESP_OK;
        LOCK();settings.enabled=false;settings.mode=MODE_OFF;phase_config_saving=false;if(!rolled_back){storage_error=true;reboot_required=true;}UNLOCK();
        return httpd_resp_send_err(req,HTTPD_500_INTERNAL_SERVER_ERROR,rolled_back?"Fahrzeugdaten nicht gespeichert. Bisherige Einstellungen beibehalten.":"Wiederherstellung unvollstaendig. Speicher pruefen; Ladung bleibt gestoppt.");
    }
    phase_config_saving=false; prepared_settings=next; prepared_count++; req->status=200; return ESP_OK;
}
