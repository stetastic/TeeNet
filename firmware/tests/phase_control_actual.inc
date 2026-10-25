static bool pv_permitted_locked(int64_t now) {

    return settings.mode==MODE_PV && settings.enabled && !charge_guard.latched &&
        (!settings.phase_switch_enabled || (phase_state==PHASE_READY && !phase_idle_inhibit)) &&

        !phase_test_active && !restart_pending() && !restarting && !ota_in_progress && !phase_mqtt_saving && !unsupported_meter() &&

        feedback_ready(now) && wallbox_ready && fresh(now,meter_at,wallbox_meter_ttl()) &&

        fresh(now,actual_at,wallbox_meter_ttl()) && fresh(now,wallbox_at,ems_param_ms(EP_SHELL_TTL)) && pv_fresh_locked(now);

}

static float unlatched_target_locked(int64_t now) {
    if(!terms_accepted || evcc_configuring || phase_mqtt_saving)return 0;
    if(evcc_control.configured && (!evcc_control.enabled || evcc_control.inhibited || evcc_control.expired ||
       evcc_control.last_contact<=0 || now<evcc_control.last_contact || now-evcc_control.last_contact>=EVCC_LEASE_MS ||
       !evcc_shell_fresh(&evcc_shell,evcc_shell_at,now) || evcc_shell.state=='F'))return 0;
    if(evcc_control.configured && settings.phase_switch_enabled &&
       (phase_idle_inhibit || evcc_desired_phases(&evcc_control,settings.charge_phases)!=settings.charge_phases))return 0;

    if(phase_config_saving || phase_test_active || restart_pending() || restarting || ota_in_progress || unsupported_meter()) return 0;
    if(settings.phase_switch_enabled && (phase_state!=PHASE_READY || phase_idle_inhibit)) return 0;
    if(charge_plan.active && plan_phase==PLAN_CLOCK)return 0;

    if(settings.mode==MODE_PV) {

        if(!pv_permitted_locked(now) || !fresh(now,pv_control.last_at,5000)) {

            return 0; /* Only status_task advances the controller state. */

        }

        float target=fminf(pv_control.target_a,settings.max_charge_a);
        if(settings.evu_input_enabled && evu_contact.stable)
            target=settings.evu_limit_a>0?fminf(target,settings.evu_limit_a):0;
        return grid_guard_target(&settings,target,house_currents_ready_locked(now));

    }

    float target=control_target(&settings,feedback_ready(now) && wallbox_ready && fresh(now,meter_at,wallbox_meter_ttl()),

        fresh(now,actual_at,wallbox_meter_ttl()),pv_fresh_locked(now));
    if(evcc_control.configured && settings.charge_phases==1)target=fminf(target,3500/settings.nominal_v);
    if(settings.evu_input_enabled && evu_contact.stable)
        target=settings.evu_limit_a>0?fminf(target,settings.evu_limit_a):0;
    return grid_guard_target(&settings,target,house_currents_ready_locked(now));

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
