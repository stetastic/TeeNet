/* Generated from main/app.c and telemetry_http.inc; do not edit. */
#include "json_owned.h"
/* Read-only submeter. This data never replaces the grid or Wallbox meter. */
static float heatpump_power_w;
static int64_t heatpump_at;
static bool heatpump_task_ready=true;

static bool heatpump_fresh_locked(int64_t now) {
    bool connected=!strcmp(settings.heatpump_source,"mqtt")?mqtt_online:wifi_online;
    return settings.heatpump_enabled&&!settings.basic_mode&&heatpump_task_ready&&connected&&
        fresh(now,heatpump_at,EMS_INPUT_TTL)&&isfinite(heatpump_power_w)&&
        heatpump_power_w>=0&&heatpump_power_w<=50000;
}
static cJSON *number_array(const float *values,int count,bool valid) {

    cJSON *a=cJSON_CreateArray(); if(!a) return NULL;

    for(int i=0;i<count;i++) json_add_owned_to_array(a,valid?cJSON_CreateNumber(values[i]):cJSON_CreateNull());

    return a;

}

static void nullable(cJSON *o,const char *key,double value,bool valid) {

    if(valid && isfinite(value)) cJSON_AddNumberToObject(o,key,value); else cJSON_AddNullToObject(o,key);

}

static void telemetry_json(cJSON *o){
    /* Called while state_lock is held. Credentials stay exclusively in NVS/RAM. */
    cJSON_AddBoolToObject(o,"telemetry_enabled",telemetry_config.enabled);
    cJSON_AddBoolToObject(o,"telemetry_decided",telemetry_config.revision[0]!=0);
    cJSON_AddBoolToObject(o,"telemetry_delete_pending",telemetry_config.delete_pending);
    cJSON_AddBoolToObject(o,"telemetry_last_ok",telemetry_last_ok);
    cJSON_AddNumberToObject(o,"telemetry_last_age_s",telemetry_last_at?(now_ms()-telemetry_last_at)/1000:-1);
    cJSON_AddStringToObject(o,"telemetry_revision",TELEMETRY_REVISION);
}
static const char *vehicle_state_name(char state) {
    switch(state) {
        case 'A': return "disconnected";
        case 'B': return "connected";
        case 'C': return "charging";
        case 'D': return "finished";
        case 'F': return "fault";
        default: return "unknown";
    }
}

static cJSON *status_json_profile(bool include_token,bool include_diagnostics) {

    cJSON *o=cJSON_CreateObject(); if(!o) return NULL;

    int64_t epoch,midnight,now=now_ms(); uint32_t date=calendar(&epoch,&midnight);

    wifi_mode_t wifi_mode=WIFI_MODE_NULL;
    wifi_ap_record_t access_point={0}; bool wifi_signal_ok=false;
    if(include_diagnostics) {
        esp_wifi_get_mode(&wifi_mode);
        wifi_signal_ok=esp_wifi_sta_get_ap_info(&access_point)==ESP_OK;
    }

    LOCK();

    bool meter_ok=feedback_ready(now) && fresh(now,meter_at,wallbox_meter_ttl()),power_ok=fresh(now,power_at,(sdm_meter()||network_wallbox_meter())?wallbox_meter_ttl():EMS_INPUT_TTL),actual_ok=feedback_ready(now) && fresh(now,actual_at,wallbox_meter_ttl());

    float reported[3]; bool house_current_ok=fresh(now,house_power_at,house_meter_ttl());

    control_report(&settings,actual_a,target_locked(now),house_power_w,house_current_ok,reported);
    grid_guard_report_locked(now,reported);

    cJSON_AddBoolToObject(o,"terms_accepted",terms_accepted);
    cJSON_AddStringToObject(o,"terms_revision",TERMS_REVISION);
    telemetry_json(o);
    cJSON_AddBoolToObject(o,"evcc_test_enabled",evcc_control.configured);
    cJSON_AddBoolToObject(o,"evcc_local_stop",evcc_control.inhibited);
    cJSON_AddBoolToObject(o,"evcc_lease_expired",evcc_control.expired);
    cJSON_AddBoolToObject(o,"evcc_status_known",evcc_shell_fresh(&evcc_shell,evcc_shell_at,now));
    if(include_diagnostics) {
        cJSON_AddStringToObject(o,"evcc_statuscar",evcc_shell.raw);
    }
    bool vehicle_status_enabled=evcc_control.configured||settings.shell_limits_auto;
    bool vehicle_status_known=vehicle_status_enabled&&shell_vehicle_fresh(&evcc_shell,evcc_shell_at,now);
    cJSON_AddBoolToObject(o,"vehicle_status_enabled",vehicle_status_enabled);
    cJSON_AddBoolToObject(o,"vehicle_status_known",vehicle_status_known);
    cJSON_AddStringToObject(o,"vehicle_connection_state",vehicle_status_known?vehicle_state_name(evcc_shell.state):"unknown");
    if(include_diagnostics) {
        cJSON_AddStringToObject(o,"vehicle_status_raw",evcc_shell.raw);
        cJSON_AddStringToObject(o,"vehicle_status_reason",evcc_shell.reason);
        nullable(o,"vehicle_status_age_s",evcc_shell_at?(now-evcc_shell_at)/1000.0:0,evcc_shell_at>0);
    }
    cJSON_AddStringToObject(o,"release_channel","stable");
    cJSON_AddStringToObject(o,"version",VERSION); cJSON_AddStringToObject(o,"build_id",EMS_BUILD_ID);
    if(include_diagnostics) {
        cJSON_AddNumberToObject(o,"rs485_modules",2);
    }
    cJSON_AddStringToObject(o,"hardware",EMS_HARDWARE_NAME);
    cJSON_AddStringToObject(o,"hardware_id",EMS_HARDWARE_ID);
    cJSON_AddNumberToObject(o,"default_shell_tx_pin",EMS_DEFAULT_SHELL_TX_PIN);
    cJSON_AddNumberToObject(o,"default_shell_rx_pin",EMS_DEFAULT_SHELL_RX_PIN);
    cJSON_AddNumberToObject(o,"default_meter_tx_pin",EMS_DEFAULT_METER_TX_PIN);
    cJSON_AddNumberToObject(o,"default_meter_rx_pin",EMS_DEFAULT_METER_RX_PIN);
    cJSON_AddNumberToObject(o,"external_mode_input_pin",settings.external_mode_input_pin);
    cJSON *gpio=cJSON_AddObjectToObject(o,"gpio_pins");
    for(unsigned i=0;i<EMS_GPIO_ROLES;i++)cJSON_AddNumberToObject(gpio,gpio_roles[i].id,settings_gpio_value(&settings,i));
    cJSON_AddBoolToObject(o,"gpio_setup_allowed",!phase_test_active&&!boot_validation_pending&&!ota_in_progress&&!restarting&&!backup_in_progress&&!evcc_configuring&&
        hardware_config_change_allowed(settings.enabled||(evcc_control.configured&&evcc_control.enabled),
            phase_config_quiescent_locked(),actual_at>0||meter_at>0,
            fresh(now,actual_at,wallbox_meter_ttl()),actual_a));
    const esp_app_desc_t *app_description=esp_app_get_description();char app_hash[65];
    for(unsigned i=0;i<32;i++)snprintf(app_hash+i*2,3,"%02x",app_description->app_elf_sha256[i]);
    cJSON_AddStringToObject(o,"app_elf_sha256",app_hash);
    cJSON_AddNumberToObject(o,"ota_max_bytes",EMS_OTA_PARTITION_BYTES);
    cJSON_AddBoolToObject(o,"diagnostic_storage_ok",fault_storage_ok);
    cJSON *ages=cJSON_AddObjectToObject(o,"measurement_age_s");
    nullable(ages,"wallbox",actual_at?(now-actual_at)/1000:0,actual_at>0);
    nullable(ages,"house",house_power_at?(now-house_power_at)/1000:0,house_power_at>0);
    nullable(ages,"battery",battery_soc_at?(now-battery_soc_at)/1000:0,battery_soc_at>0);
    nullable(ages,"pv",pv_generation_at?(now-pv_generation_at)/1000:0,pv_generation_at>0);
    cJSON_AddStringToObject(o,"mode",mode_name(settings.mode));
    cJSON_AddBoolToObject(o,"charge_plan_enabled",settings.charge_plan_enabled);
    cJSON_AddBoolToObject(o,"huawei_enabled",settings.huawei_enabled);
    cJSON_AddBoolToObject(o,"huawei_ok",fresh(now,huawei_at,15000));
    if(include_diagnostics) {
        cJSON_AddStringToObject(o,"huawei_model",huawei_model);
    }
    cJSON_AddStringToObject(o,"battery_source",settings.huawei_enabled && settings.huawei_battery?"Huawei":"MQTT");
    cJSON_AddStringToObject(o,"pv_source",settings.huawei_enabled && settings.huawei_pv?"Huawei":"MQTT");
    cJSON *plan=cJSON_AddObjectToObject(o,"charge_plan");
    cJSON_AddBoolToObject(plan,"active",charge_plan.active);
    cJSON_AddStringToObject(plan,"phase",charge_plan_phase_name(plan_phase));
    cJSON_AddNumberToObject(plan,"target_kwh",charge_plan.target_wh/1000);
    cJSON_AddNumberToObject(plan,"delivered_kwh",charge_plan.delivered_wh/1000);
    cJSON_AddNumberToObject(plan,"deadline_epoch",charge_plan.deadline_s);
    cJSON_AddBoolToObject(o,"expert_mode",settings.expert_mode);
    cJSON_AddBoolToObject(o,"expert_parameters_modified",expert_modified(&expert_saved)||settings_gpio_modified(&saved_settings));
    cJSON_AddBoolToObject(o,"phase_mqtt_allowed",phase_mqtt_allowed);
    cJSON_AddBoolToObject(o,"phase_mqtt_ready",phase_mqtt_supported_locked());
    cJSON_AddBoolToObject(o,"phase_output_base_ready",phase_switch_meter_supported(&saved_settings));
    cJSON_AddBoolToObject(o,"phase_output_setup_allowed",!phase_test_active&&phase_mqtt_stopped_locked());
    cJSON_AddBoolToObject(o,"phase_test_active",phase_test_active);
    cJSON_AddBoolToObject(o,"phase_test_ready",phase_test_ready_locked(now));
    cJSON_AddStringToObject(o,"phase_test_block",phase_test_block_locked(now));
    cJSON_AddNumberToObject(o,"phase_test_goal",phase_test_active?phase_test_goal:0);
    cJSON_AddBoolToObject(o,"phase_output_mqtt_ready",mqtt_online&&settings.mqtt_enabled);
    cJSON_AddNumberToObject(o,"phase_output_transport",saved_settings.phase_output_transport);
    cJSON_AddBoolToObject(o,"phase_output_meter_ready",phase_switch_meter_supported(&saved_settings));
    cJSON_AddNumberToObject(o,"phase_output_measured_phases",phase_measured_currents(&saved_settings));
    cJSON_AddBoolToObject(o,"phase_output_connected",phase_output_link_locked());
    cJSON_AddBoolToObject(o,"phase_mqtt_true_three_phase",saved_settings.phase_mqtt_true_three_phase);
    char phase_topic[128];
    if(phase_mqtt_output_topic(&saved_settings,phase_topic,sizeof(phase_topic)))cJSON_AddStringToObject(o,"phase_mqtt_output_topic",phase_topic);
    if(phase_mqtt_remote()&&phase_mqtt_known){
        cJSON_AddBoolToObject(o,"phase_mqtt_output_single",phase_mqtt_assumed==1);
        cJSON_AddBoolToObject(o,"phase_mqtt_output_on",phase_mqtt_output_value(&settings,phase_mqtt_assumed));
    }else{cJSON_AddNullToObject(o,"phase_mqtt_output_single");cJSON_AddNullToObject(o,"phase_mqtt_output_on");}
    cJSON_AddBoolToObject(o,"basic_mode",settings.basic_mode);
    cJSON_AddBoolToObject(o,"pv_allocation_enabled",settings.pv_allocation_enabled);
    cJSON_AddNumberToObject(o,"pv_priority",settings.pv_priority);
    cJSON_AddBoolToObject(o,"pv_allocation_ready",settings.pv_allocation_enabled && fresh(now,battery_charge_at,EMS_INPUT_TTL) && fresh(now,battery_discharge_at,EMS_INPUT_TTL) && fresh(now,battery_soc_at,EMS_INPUT_TTL));
    cJSON_AddBoolToObject(o,"vehicle_soc_enabled",settings.vehicle_soc_enabled);
    cJSON_AddBoolToObject(o,"mqtt_enabled",settings.mqtt_enabled);
    cJSON_AddBoolToObject(o,"homeassistant_enabled",settings.homeassistant_enabled);
    cJSON_AddStringToObject(o,"mqtt_input_source",settings.mqtt_input_source==2?"opendtu":settings.mqtt_input_source ? "homeassistant" : "iobroker");
    cJSON_AddBoolToObject(o,"pv_display_enabled",settings.pv_display_enabled);
    cJSON_AddBoolToObject(o,"relay_board_enabled",settings.relay_board_enabled);
    if(settings.relay_board_enabled) {
        cJSON_AddBoolToObject(o,"relay1_on",relay_on[0]);
    }
    cJSON_AddNumberToObject(o,"power_per_amp_kw",settings.charge_phases*settings.nominal_v*settings.power_factor/1000);
    cJSON_AddNumberToObject(o,"single_power_per_amp_kw",settings.nominal_v*settings.power_factor/1000);
    cJSON_AddNumberToObject(o,"phase_actuator_type",settings.phase_actuator_type);
    if(phase_shelly_remote()){
        cJSON_AddBoolToObject(o,"phase_shelly_connected",phase_shelly_observed_locked(now)!=0);
        cJSON_AddBoolToObject(o,"phase_shelly_config_ok",phase_shelly_config_ok);
        cJSON_AddBoolToObject(o,"phase_shelly_no_closed",phase_shelly_feedback_value.no_closed);
        cJSON_AddBoolToObject(o,"phase_shelly_nc_closed",phase_shelly_feedback_value.nc_closed);
        cJSON_AddBoolToObject(o,"phase_shelly_output",phase_shelly_feedback_value.output);
    }
    cJSON_AddBoolToObject(o,"phase_switch_enabled",settings.phase_switch_enabled);
    cJSON_AddBoolToObject(o,"phase_feedback_enabled",settings.phase_feedback_enabled);
    cJSON_AddNumberToObject(o,"phase_feedback_count",settings.phase_feedback_count);
    bool contact_values_ok=settings.phase_switch_enabled&&settings.phase_feedback_enabled&&
        (phase_shelly_remote()?(phase_shelly_config_ok&&fresh(now,phase_shelly_at,SHELLY_PHASE_TTL_MS)):
         settings.phase_actuator_type==0&&phase_gpio_ready);
    if(contact_values_ok){
        cJSON_AddBoolToObject(o,"phase_contact1_closed",phase_shelly_remote()?phase_shelly_feedback_value.no_closed:gpio_get_level(PHASE_FEEDBACK_GPIO)==0);
        if(settings.phase_feedback_count==2)
            cJSON_AddBoolToObject(o,"phase_contact2_closed",phase_shelly_remote()?phase_shelly_feedback_value.nc_closed:gpio_get_level(PHASE_FEEDBACK_SECOND_GPIO)==0);
        else cJSON_AddNullToObject(o,"phase_contact2_closed");
    }else{
        cJSON_AddNullToObject(o,"phase_contact1_closed");cJSON_AddNullToObject(o,"phase_contact2_closed");
    }
    cJSON_AddBoolToObject(o,"phase_feedback_closed_is_single",settings.phase_feedback_closed_is_single);
    cJSON_AddNumberToObject(o,"fixed_charge_phases",settings.fixed_charge_phases);
    cJSON_AddBoolToObject(o,"shell_limits_auto",settings.shell_limits_auto);
    cJSON_AddBoolToObject(o,"shell_limits_ok",settings.shell_limits_auto && fresh(now,shell_limits_at,900000));
    if(include_diagnostics) {
        cJSON_AddStringToObject(o,"shell_setup_host",shell_active_host);
        cJSON_AddNumberToObject(o,"shell_limits_http",shell_limits_code);
        nullable(o,"shell_grid_limit_a",shell_grid_a,shell_limits_at>0);
        nullable(o,"shell_max_charge_a",shell_max_a,shell_limits_at>0);
    }
    cJSON_AddNumberToObject(o,"charge_phases",settings.charge_phases);
    cJSON_AddNumberToObject(o,"manual_phases",settings.manual_phases);
    cJSON_AddNumberToObject(o,"phase_feedback",phase_gpio_ready?phase_observed():0);
    cJSON_AddBoolToObject(o,"phase_idle_inhibit",phase_idle_inhibit);
    cJSON_AddStringToObject(o,"phase_state",phase_state==PHASE_READY?"bereit":phase_state==PHASE_STOPPING?"stoppen":phase_state==PHASE_MOVING?"schalten":phase_state==PHASE_SETTLING?"pruefen":"fehler");
    /* Expose real state-machine deadlines; UI never invents a countdown. */
    int64_t phase_deadline=0; const char *phase_wait="none";
    phase_wait_status_locked(now,&phase_wait,&phase_deadline);
    cJSON_AddStringToObject(o,"phase_wait_kind",phase_wait);
    cJSON_AddNumberToObject(o,"phase_wait_s",phase_deadline>now?(phase_deadline-now+999)/1000:0);
    cJSON_AddBoolToObject(o,"control_status_visible",true);
    cJSON_AddBoolToObject(o,"can_unlock",phase_state==PHASE_FAULT?phase_can_unlock_locked(now):charge_guard.latched);
    cJSON_AddNumberToObject(o,"grid_price_kwh",settings.price_kwh);
    cJSON_AddNumberToObject(o,"solar_price_kwh",settings.solar_price_kwh);

    cJSON_AddBoolToObject(o,"enabled",settings.enabled); cJSON_AddStringToObject(o,"block_reason",block_reason_locked(now));

    cJSON_AddBoolToObject(o,"control_verified",settings.control_verified);

    cJSON_AddNumberToObject(o,"pv_wait_s",(pv_control.wait_ms+999)/1000);

    cJSON_AddNumberToObject(o,"min_charge_a",settings.min_charge_a);

    cJSON_AddBoolToObject(o,"charge_stop_latched",charge_guard.latched);
    cJSON_AddNumberToObject(o,"charge_retry_s",charge_guard.retry_until>now?(charge_guard.retry_until-now+999)/1000:0);
    cJSON_AddNumberToObject(o,"charge_stop_count",charge_guard.stop_count);
    cJSON_AddNumberToObject(o,"current_offset_a",settings.current_offset_a);
    cJSON_AddStringToObject(o,"calibration_state",current_calibration.since?"learning":current_calibration.adjustments?"adjusted":"waiting");

    cJSON_AddBoolToObject(o,"battery_protect",settings.battery_protect);
    cJSON_AddBoolToObject(o,"battery_use",battery_use);
    cJSON_AddBoolToObject(o,"battery_start_use",battery_start_use);
    cJSON_AddBoolToObject(o,"battery_buffer_active",battery_buffer.active);
    cJSON_AddBoolToObject(o,"battery_buffer_expired",battery_buffer.started && now-battery_buffer.started_at>=EMS_BATTERY_BUFFER_MS);
    cJSON_AddNumberToObject(o,"battery_buffer_duration_s",EMS_BATTERY_BUFFER_MS/1000);
    int64_t cloud_left=battery_buffer.started?EMS_BATTERY_BUFFER_MS-(now-battery_buffer.started_at):0;
    cJSON_AddNumberToObject(o,"battery_buffer_remaining_s",cloud_left>0?(cloud_left+999)/1000:0);
    cJSON_AddBoolToObject(o,"battery_assist_active",battery_buffer.assist_active);
    cJSON_AddBoolToObject(o,"battery_power_ok",battery_power_locked(now).fresh);
    cJSON_AddBoolToObject(o,"control_storage_ok",control_storage_ok);
    cJSON_AddNumberToObject(o,"battery_reserve_soc",settings.battery_reserve_soc);
    cJSON_AddNumberToObject(o,"battery_capacity_kwh",EMS_BATTERY_CAPACITY_KWH);
    cJSON_AddNumberToObject(o,"battery_discharge_limit_w",settings.battery_assist_limit_w);
    cJSON_AddNumberToObject(o,"battery_cloud_limit_w",settings.battery_cloud_limit_w);

    cJSON_AddBoolToObject(o,"battery_ok",battery_ready_locked(now));
    cJSON_AddBoolToObject(o,"battery_soc_ok",fresh(now,battery_soc_at,EMS_INPUT_TTL));

    nullable(o,"battery_soc_pct",battery_soc,fresh(now,battery_soc_at,EMS_INPUT_TTL));
    nullable(o,"car_soc_pct",car_soc,settings.vehicle_soc_enabled && fresh(now,car_soc_at,ems_param_ms(EP_CAR_TTL)));
    nullable(o,"pv_generation_w",pv_generation_w,settings.pv_display_enabled && fresh(now,pv_generation_at,EMS_INPUT_TTL));
    bool heatpump_ok=heatpump_fresh_locked(now);
    cJSON_AddBoolToObject(o,"heatpump_enabled",settings.heatpump_enabled&&!settings.basic_mode);
    cJSON_AddBoolToObject(o,"heatpump_ok",heatpump_ok);
    nullable(o,"heatpump_w",heatpump_power_w,heatpump_ok);
    nullable(o,"heatpump_age_s",heatpump_at?(now-heatpump_at)/1000.0:0,heatpump_at>0);
    cJSON_AddStringToObject(o,"heatpump_source",settings.heatpump_source);
    cJSON_AddBoolToObject(o,"heatpump_publish_enabled",settings.heatpump_publish_enabled);
    cJSON_AddBoolToObject(o,"heatpump_publish_ok",heatpump_ok&&mqtt_online&&settings.heatpump_publish_enabled);
    cJSON_AddStringToObject(o,"heatpump_mqtt_topic",settings.heatpump_output_topic);

    battery_power_t battery_power=battery_power_locked(now);
    nullable(o,"battery_charge_w",battery_power.charge_w,battery_power.fresh);
    nullable(o,"battery_discharge_w",battery_power.discharge_w,battery_power.fresh);

    cJSON_AddNumberToObject(o,"pv_start_threshold_a",battery_start_use&&settings.battery_protect&&battery_ready_locked(now)&&battery_soc>settings.battery_reserve_soc?active_min_a():pv_start_threshold(&settings));

    cJSON_AddNumberToObject(o,"pv_stop_threshold_a",active_min_a()-ems_param(EP_PV_DEFICIT));

    cJSON_AddBoolToObject(o,"meter_ok",meter_ok); cJSON_AddBoolToObject(o,"wallbox_ok",fresh(now,wallbox_at,ems_param_ms(EP_SHELL_TTL)));

    cJSON_AddBoolToObject(o,"feedback_ok",actual_ok); cJSON_AddBoolToObject(o,"mqtt_ok",mqtt_online); cJSON_AddBoolToObject(o,"wifi_ok",wifi_online);
    if(include_diagnostics) {
        nullable(o,"wifi_rssi_dbm",access_point.rssi,wifi_signal_ok);
    }

    cJSON_AddBoolToObject(o,"wallbox_uart_ready",wallbox_ready); cJSON_AddBoolToObject(o,"meter_uart_ready",meter_ready);

    cJSON_AddBoolToObject(o,"reboot_required",reboot_required||expert_reboot_pending);
    cJSON_AddBoolToObject(o,"firmware_check_pending",boot_validation_pending);
    cJSON_AddBoolToObject(o,"firmware_check_ok",boot_validation_ok);
    cJSON_AddBoolToObject(o,"firmware_check_failed",boot_validation_failed);
    cJSON_AddBoolToObject(o,"runtime_watchdog_failed",runtime_watchdog_failed);
    cJSON_AddNumberToObject(o,"runtime_watchdog_tasks",runtime_watchdog_tasks);
    cJSON_AddBoolToObject(o,"ota_rollback_armed",boot_rollback_armed); cJSON_AddBoolToObject(o,"clock_ok",date!=0);

    if(include_diagnostics) {
        cJSON_AddNumberToObject(o,"current_date",date);
    }

    cJSON_AddBoolToObject(o,"storage_ok",stats_storage_ok && config_storage_ok && !storage_error && plan_storage_ok && control_storage_ok);

    if(include_diagnostics) {
        cJSON_AddNumberToObject(o,"uptime_s",now/1000);
        cJSON_AddNumberToObject(o,"min_free_heap",heap_caps_get_minimum_free_size(MALLOC_CAP_8BIT));
        cJSON_AddNumberToObject(o,"largest_free_block",heap_caps_get_largest_free_block(MALLOC_CAP_8BIT));cJSON_AddNumberToObject(o,"reset_reason",esp_reset_reason());

        cJSON_AddNumberToObject(o,"meter_failures",meter_failures); cJSON_AddNumberToObject(o,"wallbox_requests",wallbox_requests);

        cJSON_AddNumberToObject(o,"wallbox_rx_bytes",wallbox_rx_bytes); cJSON_AddNumberToObject(o,"wallbox_valid_frames",wallbox_valid_frames);

        cJSON_AddNumberToObject(o,"wallbox_other_address",wallbox_other_address); cJSON_AddNumberToObject(o,"wallbox_crc_resyncs",wallbox_crc_resyncs);

        nullable(o,"wallbox_last_address",wallbox_last_address,wallbox_valid_frames>0); nullable(o,"wallbox_last_function",wallbox_last_function,wallbox_valid_frames>0);

        nullable(o,"wallbox_last_register",wallbox_last_register,wallbox_valid_frames>0);

        nullable(o,"wallbox_last_count",wallbox_last_count,wallbox_valid_frames>0);

        nullable(o,"wallbox_reply_age_s",(now-wallbox_at)/1000,wallbox_at>0);

        cJSON_AddNumberToObject(o,"wallbox_tx_errors",wallbox_tx_errors);

        char rx_hex[sizeof(wallbox_rx_tail)*2+1]; const char *hex="0123456789ABCDEF";

        for(size_t i=0;i<wallbox_rx_count;i++) {

            uint8_t byte=wallbox_rx_tail[(wallbox_rx_next+sizeof(wallbox_rx_tail)-wallbox_rx_count+i)%sizeof(wallbox_rx_tail)];

            rx_hex[i*2]=hex[byte>>4]; rx_hex[i*2+1]=hex[byte&15];

        }

        rx_hex[wallbox_rx_count*2]=0; cJSON_AddStringToObject(o,"wallbox_rx_tail_hex",rx_hex);

        nullable(o,"wallbox_rx_age_ms",now-wallbox_rx_at,wallbox_rx_count>0);

        cJSON_AddNumberToObject(o,"mqtt_rejected",mqtt_rejected); cJSON_AddNumberToObject(o,"saved_ago_s",(now-last_saved_ms)/1000);
    }

    cJSON_AddNumberToObject(o,"grid_limit_a",settings.grid_limit_a); cJSON_AddNumberToObject(o,"max_charge_a",settings.max_charge_a);

    cJSON_AddNumberToObject(o,"current_a",settings.manual_current_a); cJSON_AddNumberToObject(o,"target_current_a",target_locked(now));
    cJSON_AddNumberToObject(o,"manual_power_kw",settings.manual_current_a*settings.manual_phases*settings.nominal_v*settings.power_factor/1000);

    cJSON_AddBoolToObject(o,"stop_requested",target_locked(now)<=0);

    cJSON_AddBoolToObject(o,"stop_current_below_1a",target_locked(now)<=0 && actual_ok && actual_a[0]<1 && actual_a[1]<1 && actual_a[2]<1);

    if(include_diagnostics) {
        json_add_owned_to_object(o,"last_sent_a",number_array(last_sent_a,3,last_sent_current_at>0));
    }

    nullable(o,"last_sent_age_ms",now-last_sent_current_at,last_sent_current_at>0);

    cJSON_AddNumberToObject(o,"pv_surplus_a",settings.pv_surplus_a);
    if(include_diagnostics) {
        cJSON_AddStringToObject(o,"station_ip",station_ip);
    }

    cJSON_AddNumberToObject(o,"xemex_coils",settings.xemex_coils);

    cJSON_AddStringToObject(o,"wallbox_meter_type",settings.wallbox_meter_type);

    cJSON_AddStringToObject(o,"wallbox_power_source",(sdm_meter()||network_wallbox_meter())?settings.wallbox_meter_type:"mqtt");

    bool extrapolated=(sdm_meter()&&sdm_profile(settings.wallbox_meter_type)->phases==1)||(!strcmp(settings.wallbox_meter_type,"xemex")&&settings.xemex_coils<3);
    cJSON_AddBoolToObject(o,"power_is_extrapolated",extrapolated);

    cJSON_AddBoolToObject(o,"house_meter_ok",house_power_capable()?fresh(now,house_power_at,house_meter_ttl()):fresh(now,house_current_at,house_meter_ttl()));
    cJSON_AddBoolToObject(o,"house_power_ok",fresh(now,house_power_at,house_meter_ttl()));
    bool house_phases_ok=house_currents_ready_locked(now);
    cJSON_AddBoolToObject(o,"house_current_ok",house_phases_ok);
    cJSON_AddBoolToObject(o,"grid_guard_enabled",settings.grid_guard_enabled);
    cJSON_AddBoolToObject(o,"grid_guard_ok",!settings.grid_guard_enabled || house_phases_ok);
    cJSON_AddBoolToObject(o,"grid_guard_fallback",settings.grid_guard_enabled && !house_phases_ok);
    nullable(o,"grid_guard_limit_kw",EMS_GRID_FALLBACK_W/1000,settings.grid_guard_enabled && !house_phases_ok);
    if(include_diagnostics) {
        json_add_owned_to_object(o,"house_a",number_array(house_a,3,house_phases_ok));
    }
    cJSON_AddBoolToObject(o,"external_mode_input_enabled",settings.external_mode_input_enabled);
    cJSON_AddBoolToObject(o,"external_mode_contact",settings.external_mode_input_enabled && mode_contact.stable);
    cJSON_AddBoolToObject(o,"evu_input_enabled",settings.evu_input_enabled);
    cJSON_AddBoolToObject(o,"evu_active",settings.evu_input_enabled && evu_contact.stable);
    cJSON_AddNumberToObject(o,"evu_limit_a",settings.evu_limit_a);

    if(include_diagnostics) {
        cJSON_AddBoolToObject(o,"ap_active",wifi_mode==WIFI_MODE_AP || wifi_mode==WIFI_MODE_APSTA);

        cJSON_AddBoolToObject(o,"ap_forced",wifi_ap_forced);

        cJSON_AddStringToObject(o,"ap_ssid",SETUP_SSID);
        cJSON_AddStringToObject(o,"hostname",DEVICE_HOSTNAME);

        cJSON_AddBoolToObject(o,"wifi_fallback",wifi_fallback);
    }

    nullable(o,"meter_total_a",settings.charge_phases==1?meter_a[0]:meter_a[0]+meter_a[1]+meter_a[2],meter_ok);

    cJSON_AddBoolToObject(o,"zero_feed_enabled",settings.zero_feed_enabled);

    cJSON_AddStringToObject(o,"house_meter_type",settings.house_meter_type);

    nullable(o,"house_power_w",house_power_w,fresh(now,house_power_at,house_meter_ttl()));

    cJSON_AddStringToObject(o,"mqtt_prefix",settings.mqtt_prefix);

    json_add_owned_to_object(o,"meter_a",number_array(meter_a,3,meter_ok)); json_add_owned_to_object(o,"actual_a",number_array(actual_a,3,actual_ok));

    json_add_owned_to_object(o,"reported_a",number_array(reported,3,true));

    nullable(o,"estimate_w",estimated_charge_power(&settings,meter_a),meter_ok);

    nullable(o,"wallbox_w",wallbox_w,power_ok);

    cJSON *sources=cJSON_AddArrayToObject(o,"energy");

    for(int s=0;s<2;s++) {

        cJSON *item=cJSON_CreateObject(); double today=0; uint64_t covered=0,charging=0;

        for(int d=0;d<EMS_DAYS;d++) if(energy.store.days[d].date==date && date) { today=energy.store.days[d].wh[s]; covered=energy.store.days[d].covered_ms[s]; charging=energy.store.days[d].charging_ms; }

        cJSON_AddStringToObject(item,"source",s==0?"current_estimate":sdm_meter()?settings.wallbox_meter_type:"wallbox_mqtt");

        cJSON_AddNumberToObject(item,"total_kwh",fmax(0,energy.store.total_wh[s]-energy_reset_wh[s])/1000); cJSON_AddNumberToObject(item,"boot_kwh",energy.boot_wh[s]/1000);
        cJSON_AddNumberToObject(item,"total_reset_date",energy_reset_date);

        cJSON_AddNumberToObject(item,"undated_kwh",energy.store.undated_wh[s]/1000);

        nullable(item,"today_kwh",today/1000,date!=0 && covered>0); cJSON_AddNumberToObject(item,"today_covered_s",covered/1000);
        cJSON_AddNumberToObject(item,"today_charging_s",charging/1000);

        nullable(item,"today_cost",today/1000*settings.price_kwh,date!=0 && covered>0); json_add_owned_to_array(sources,item);

    }

    if(!include_diagnostics)cJSON_AddStringToObject(o,"status_profile","dashboard");

    if(include_token) cJSON_AddStringToObject(o,"token",csrf_token);

    UNLOCK(); return o;

}

/* Legacy HTTP and MQTT clients retain the complete status schema. */
static cJSON *status_json(bool include_token) {
    return status_json_profile(include_token,true);
}

