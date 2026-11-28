static void arm_wired_backup_locked(int64_t now){
    if(settings.basic_mode||!wifi_online||!wallbox_control_ready_locked(now))return;
    if(settings.shell_native_fallback){
        if(fresh(now,evcc_shell_at,EVCC_SHELL_TTL_MS)&&evcc_shell.state=='A'){
            wired_backup_armed=false;return;
        }
        if(wired_backup_armed)return;
        bool charging=settings.shell_http_control_enabled
            ? fresh(now,evcc_shell_at,EVCC_SHELL_TTL_MS)&&evcc_shell.http_control.current_valid&&evcc_shell.http_control.current_a>1
            : fresh(now,actual_at,wallbox_meter_ttl())&&actual_a[0]>1;
        if(!charging)return;
    }
    if(wired_backup_armed)return;
    wired_backup_armed=settings.grid_backup_enabled&&settings.enabled&&!charge_plan.active&&
        !charge_guard.latched&&!evcc_control.configured&&!settings.grid_guard_enabled&&
        settings.shell_rs485_interface!=1&&settings.shell_limits_auto&&(shell_limits_code==200||shell_limits_code==0)&&
        fresh(now,shell_limits_at,900000)&&shell_connector_max_a==settings.max_charge_a&&
        fresh(now,wallbox_at,ems_param_ms(EP_SHELL_TTL))&&
        (!settings.phase_switch_enabled||(phase_shelly_remote()&&!phase_idle_inhibit&&phase_state==PHASE_READY))&&
        (!settings.shell_http_control_enabled||(http_regulator.running&&!http_regulator.control.fault));
}

static bool wired_backup_locked(int64_t now){
    if(settings.basic_mode||!wired_backup_armed||!settings.grid_backup_enabled||!settings.enabled||evcc_control.configured||
       charge_plan.active||settings.grid_guard_enabled||settings.shell_rs485_interface==1||
       !wallbox_ready||(!settings.shell_native_fallback&&!fresh(now,wallbox_at,ems_param_ms(EP_SHELL_TTL)))||
       !settings.shell_limits_auto||!shell_limits_at||shell_connector_max_a!=settings.max_charge_a||
       shell_grid_a!=settings.grid_limit_a||shell_grid_a<shell_connector_max_a)return false;
    if((fresh(now,evcc_shell_at,EVCC_SHELL_TTL_MS)&&(evcc_shell.state=='F'||
        (settings.shell_native_fallback&&evcc_shell.state=='A')))||
       (settings.shell_http_control_enabled&&http_regulator.control.fault))return false;
    if(settings.mode!=MODE_PV&&(settings.mode!=MODE_MANUAL||settings.manual_current_a<settings.min_charge_a))return false;
    if(settings.evu_input_enabled&&evu_contact.stable)return false;
    if(settings.phase_switch_enabled){
        if(!phase_shelly_remote()||phase_idle_inhibit||phase_startup||
           (phase_state!=PHASE_READY&&!(phase_state==PHASE_SETTLING&&phase_shelly_rechecking))||
           phase_goal!=settings.charge_phases||phase_shelly_wrong_position_locked(now,phase_goal))return false;
        if(phase_uses_voltage(&settings)&&fresh(now,phase_voltage.sample_at,PHASE_VOLTAGE_TTL_MS)&&
           phase_voltage_position(phase_voltage.voltage)!=phase_goal)return false;
        if(fresh(now,actual_at,wallbox_meter_ttl())&&phase_current_mismatch(&settings,actual_a))return false;
    }
    bool missing_energy=settings.shell_native_fallback&&((settings.mode==MODE_PV&&!pv_fresh_locked(now))||
        (settings.zero_feed_enabled&&!fresh(now,house_power_at,house_meter_ttl())));
    return missing_energy||!wifi_online||wallbox_meter_hold_locked(now)||!wallbox_control_ready_locked(now)||
        (phase_shelly_rechecking&&!phase_shelly_backup_position_locked(now));
}

static bool charge_position_ready_locked(int64_t now){
    return !settings.phase_switch_enabled||(!phase_idle_inhibit&&
        (phase_state==PHASE_READY||phase_shelly_backup_position_locked(now)));
}

static bool grid_backup_requested_locked(int64_t now){
    if(settings.basic_mode||!settings.grid_backup_enabled||settings.shell_native_fallback||!settings.enabled||evcc_control.configured||
       !wallbox_ready||!fresh(now,wallbox_at,ems_param_ms(EP_SHELL_TTL)))return false;
    if(settings.mode!=MODE_PV&&(settings.mode!=MODE_MANUAL||settings.manual_current_a<settings.min_charge_a))return false;
    if(!wallbox_control_ready_locked(now)||wallbox_meter_hold_locked(now))return false;
    return phase_shelly_backup_position_locked(now)||(settings.mode==MODE_PV&&!pv_fresh_locked(now))||
        (settings.zero_feed_enabled&&!fresh(now,house_power_at,house_meter_ttl()));
}

static bool pv_permitted_locked(int64_t now) {

    return settings.mode==MODE_PV && settings.enabled && !charge_guard.latched &&
        charge_position_ready_locked(now) &&

        !phase_test_active && !restart_pending() && !restarting && !ota_in_progress && !phase_mqtt_saving && !unsupported_meter() &&

        wallbox_control_ready_locked(now) && wallbox_ready &&

        fresh(now,wallbox_at,ems_param_ms(EP_SHELL_TTL)) && pv_fresh_locked(now);

}

static float unlatched_target_locked(int64_t now) {
#if CONFIG_TEE_HTTP_CONTROL_TRIAL
    if(!normal_offset_trial_ready(&normal_offset_trial,now))return 0;
    if(shell_http_trial.armed&&(settings.mode!=MODE_MANUAL||
       !shell_http_trial_ready(&shell_http_trial,&evcc_shell.http_control,evcc_shell_at,now)||
       !shell_vehicle_fresh(&evcc_shell,evcc_shell_at,now)||evcc_shell.state=='F'))return 0;
#endif
    bool wired=wired_backup_locked(now);
    if(settings.shell_http_control_enabled&&!wired&&(!http_regulation_ready_locked(now)||!http_regulator.running))return 0;
    if(!terms_accepted || evcc_configuring || phase_mqtt_saving)return 0;
    if(evcc_control.configured && (!evcc_control.enabled || evcc_control.inhibited || evcc_control.expired ||
       evcc_control.last_contact<=0 || now<evcc_control.last_contact || now-evcc_control.last_contact>=EVCC_LEASE_MS ||
       !evcc_shell_fresh(&evcc_shell,evcc_shell_at,now) || evcc_shell.state=='F'))return 0;
    if(evcc_control.configured && settings.phase_switch_enabled &&
       (phase_idle_inhibit || evcc_desired_phases(&evcc_control,settings.charge_phases)!=settings.charge_phases))return 0;

    if(phase_config_saving || phase_test_active || restart_pending() || restarting || ota_in_progress || unsupported_meter()) return 0;
    if(!wired&&!charge_position_ready_locked(now))return 0;
    /* A manual phase request is committed before the controller's candidate
       debounce. Keep UART start permission blocked on the old physical phases. */
    if(settings.phase_switch_enabled && !evcc_control.configured && settings.mode==MODE_MANUAL &&
       settings.manual_phases!=settings.charge_phases) return 0;
    if(charge_plan.active && plan_phase==PLAN_CLOCK)return 0;

    if(wired)return settings.max_charge_a;
    if(grid_backup_requested_locked(now)){
        float target=settings.max_charge_a;
        if(settings.evu_input_enabled&&evu_contact.stable)target=fminf(target,settings.evu_limit_a);
        return grid_guard_target(&settings,target,house_currents_ready_locked(now));
    }

    if(settings.mode==MODE_PV) {

        if(!pv_permitted_locked(now) || !fresh(now,pv_control.last_at,5000)) {

            return 0; /* Only status_task advances the controller state. */

        }

        float target=fminf(pv_control.target_a,settings.max_charge_a);
        if(settings.evu_input_enabled && evu_contact.stable)
            target=settings.evu_limit_a>0?fminf(target,settings.evu_limit_a):0;
        return grid_guard_target(&settings,target,house_currents_ready_locked(now));

    }

    float target=control_target(&settings,wallbox_ready && wallbox_control_ready_locked(now),

        wallbox_control_ready_locked(now),pv_fresh_locked(now));
    if(evcc_control.configured && settings.charge_phases==1)target=fminf(target,3500/ems_phase_voltage_sum(&settings,1));
    if(settings.evu_input_enabled && evu_contact.stable)
        target=settings.evu_limit_a>0?fminf(target,settings.evu_limit_a):0;
    return grid_guard_target(&settings,target,house_currents_ready_locked(now));

}

static float target_locked(int64_t now) {

    float requested=unlatched_target_locked(now);
    if(wallbox_meter_hold_locked(now)&&!wired_backup_locked(now))requested=fminf(requested,wallbox_meter_hold.target_a);

    return charge_guard_blocked(&charge_guard,now)?0:vehicle_limit_current_locked(requested);

}

static bool shell_decides_locked(int64_t now,float permitted){
    return settings.shell_native_fallback&&permitted==settings.max_charge_a&&wired_backup_locked(now);
}

static size_t shell_meter_reply_locked(const uint8_t *req,uint8_t *response,float currents[3],int64_t now,float permitted){
    /* The Shell may increase its polling interval when it gets no reply.
       Silence is independent of the age of the last transmission. */
    if(shell_decides_locked(now,permitted))return 0;
    return modbus_reply(req,8,response,&settings,currents);
}

static void idle_resume_command(settings_t next,bool restart) {
        if(phase_idle_inhibit && next.enabled &&
           (restart || !settings.enabled || next.mode!=settings.mode ||
            next.manual_phases!=settings.manual_phases ||
            fabsf(next.manual_current_a-settings.manual_current_a)>=0.05f)) {
            phase_idle_inhibit=false; phase_hold_until=0;
        }
}
static bool evcc_ready_locked(int64_t now) {
    return terms_accepted && evcc_shell_fresh(&evcc_shell,evcc_shell_at,now) && evcc_shell.state!='F' &&
        feedback_ready(now) && fresh(now,meter_at,wallbox_meter_ttl()) && fresh(now,actual_at,wallbox_meter_ttl()) &&
        wallbox_ready && fresh(now,wallbox_at,ems_param_ms(EP_SHELL_TTL)) && !phase_config_saving && !phase_test_active && !restart_pending() && !restarting && !ota_in_progress && !phase_mqtt_saving &&
        !charge_guard_blocked(&charge_guard,now) && (!settings.phase_switch_enabled || (phase_gpio_ready && phase_state!=PHASE_FAULT && !phase_idle_inhibit));
}

static esp_err_t evcc_release_post(httpd_req_t *req) {
    if(!authorized(req))return ESP_OK;
    cJSON *o=read_json(req);if(!o)return ESP_OK;
    cJSON *value=cJSON_GetObjectItemCaseSensitive(o,"release");
    bool shape=cJSON_IsTrue(value) && o->child==value && !value->next;
    LOCK();bool ok=shape && evcc_local_release(&evcc_control,now_ms());
    if(ok){
        settings.enabled=false;settings.mode=MODE_OFF;
        /* Only this explicit local action rearms an idle one-phase attempt.
           Polls and automatic evcc commands must retain the idle lock. */
        phase_idle_inhibit=false;phase_attempt_at=phase_no_load_since=phase_hold_until=0;
        event_locked("evcc_released");
    }
    UNLOCK();cJSON_Delete(o);
    if(!ok){httpd_resp_set_status(req,"409 Conflict");return httpd_resp_sendstr(req,"Keine lokale evcc-Sperre aktiv.");}
    return ok_json(req);
}
