static bool apply_control(cJSON *o) {
    if(!terms_accepted)return false;

    if(!cJSON_IsObject(o) || !o->child) return false;

    int64_t command_epoch,command_midnight;uint32_t command_date=calendar(&command_epoch,&command_midnight);
    LOCK();
    if(evcc_configuring || phase_mqtt_saving){UNLOCK();return false;}
#if CONFIG_TEE_HTTP_CONTROL_TRIAL
    /* Test clients must not overwrite a user command arriving between their
       last observation and this mutation. Check the revision under the same
       lock as the control update. Unconditional Stop remains available. */
    const cJSON *expected_sequence=cJSON_GetObjectItemCaseSensitive(o,"expected_event_sequence");
    if(expected_sequence&&(!cJSON_IsNumber(expected_sequence)||!isfinite(expected_sequence->valuedouble)||
       expected_sequence->valuedouble<0||expected_sequence->valuedouble>UINT32_MAX||
       expected_sequence->valuedouble!=(double)event_log.sequence)){UNLOCK();return false;}
    for(const cJSON *item=expected_sequence?expected_sequence->next:NULL;item;item=item->next)
        if(!strcmp(item->string,"expected_event_sequence")){UNLOCK();return false;}
#endif
    if(evcc_control.configured) {
        char *body=cJSON_PrintUnformatted(o);
        bool stop=evcc_emergency_stop_json(body);free(body);
        if(!stop){UNLOCK();return false;}
        evcc_local_stop(&evcc_control);
    }
    settings_t next=settings; bool valid=true,pv=false,next_battery_use=battery_use,next_battery_start_use=battery_start_use,restart=false;
    bool manual_phases_explicit=false,manual_mode_requested=false,persist_preferences=false;
    int64_t change_at=now_ms();float previous_target=target_locked(change_at);control_mode_t previous_mode=settings.mode;
    bool transition=false;
    bool user_override=cJSON_GetObjectItem(o,"mode")||cJSON_GetObjectItem(o,"enabled")||cJSON_GetObjectItem(o,"current_a");

    for(cJSON *i=o->child;i;i=i->next) {

#if CONFIG_TEE_HTTP_CONTROL_TRIAL
        if(!strcmp(i->string,"expected_event_sequence"))continue;
#endif
        if(!strcmp(i->string,"mode")) { valid=cJSON_IsString(i) && parse_mode(i->valuestring,&next.mode); if(valid && next.mode==MODE_MANUAL) manual_mode_requested=true; }
        else if(!strcmp(i->string,"manual_phases")) { valid=cJSON_IsNumber(i) && (i->valuedouble==1 || i->valuedouble==3) && (i->valueint==(settings.fixed_charge_phases) || settings.phase_switch_enabled); if(valid){next.manual_phases=i->valueint;manual_phases_explicit=true;} }

        else if(!strcmp(i->string,"restart")) { valid=cJSON_IsBool(i); restart=cJSON_IsTrue(i); }
        else if(!strcmp(i->string,"battery_use")) { valid=cJSON_IsBool(i); next_battery_use=cJSON_IsTrue(i); }
        else if(!strcmp(i->string,"battery_start_use")) { valid=cJSON_IsBool(i); next_battery_start_use=cJSON_IsTrue(i); }
        else if(!strcmp(i->string,"enabled")) { valid=cJSON_IsBool(i); next.enabled=cJSON_IsTrue(i); }

        else if(!strcmp(i->string,"current_a") || !strcmp(i->string,"pv_surplus_a")) {

            valid=cJSON_IsNumber(i) && isfinite(i->valuedouble) && i->valuedouble>=0 && i->valuedouble<=next.max_charge_a;

            if(valid && !strcmp(i->string,"current_a")) next.manual_current_a=i->valuedouble;

            else if(valid) { next.pv_surplus_a=i->valuedouble; pv=true; }

        } else valid=false;

        if(!valid) break;

    }

    if(manual_mode_requested && !manual_phases_explicit) next.manual_phases=next.phase_switch_enabled?settings.charge_phases:next.fixed_charge_phases;
    if(manual_mode_requested && previous_mode==MODE_PV && previous_target>0 && !cJSON_GetObjectItem(o,"current_a"))next.manual_current_a=previous_target;
    if(!next.phase_switch_enabled && next.manual_phases!=next.fixed_charge_phases)valid=false;
    if(settings.basic_mode && (next.mode==MODE_PV || next.mode==MODE_GRID_LIMIT || pv || next_battery_use || next_battery_start_use))valid=false;
    if((next_battery_use || next_battery_start_use) && (!settings.battery_protect || !settings.zero_feed_enabled)) valid=false;
    if(next.mode==MODE_OFF) next.enabled=false;
    if(restart && phase_state==PHASE_FAULT && !phase_can_unlock_locked(now_ms())) valid=false;

    if(valid && (!pv || !settings.zero_feed_enabled) && !restarting && !ota_in_progress && ((!phase_test_active&&!phase_config_saving) || !next.enabled) && (!restart_pending() || !next.enabled)) {

        /* Restore before changing preferences or starting a controller. A
           later persistence pass must not overwrite newly started timers. */
        restore_control_memory_locked(change_at,command_date?command_epoch:0,true);

        transition=next.mode!=settings.mode || next.enabled!=settings.enabled;
        if(transition||next.manual_phases!=settings.manual_phases)memset(&wallbox_meter_hold,0,sizeof(wallbox_meter_hold));
        if(phase_test_active&&user_override&&!next.enabled)phase_test_end_locked();

        if((restart && charge_guard.latched) || (next.enabled && !settings.enabled)) memset(&charge_guard,0,sizeof(charge_guard));
        if(restart && phase_can_unlock_locked(now_ms())) {
            phase_goal=phase_shelly_remote()?phase_observed():3;
            if(phase_shelly_remote()){phase_shelly_uncertain=false;phase_shelly_pending=false;}
            if(phase_mqtt_remote()){
                phase_mqtt_pending=false;phase_mqtt_known=false;phase_goal=3;
                phase_stop_at=now_ms();phase_zero_at=0;phase_state=PHASE_STOPPING;
            }else {settings.charge_phases=phase_goal;phase_move_at=now_ms();phase_state=PHASE_SETTLING;}
        }
        if(phase_idle_inhibit && next.enabled &&
           (restart || !settings.enabled || next.mode!=settings.mode ||
            next.manual_phases!=settings.manual_phases ||
            fabsf(next.manual_current_a-settings.manual_current_a)>=0.05f)) {
            phase_idle_inhibit=false; phase_hold_until=0;
        }

        if(next_battery_use!=battery_use || next_battery_start_use!=battery_start_use) {
            persist_preferences=true;
            battery_use=next_battery_use;
            battery_start_use=next_battery_start_use;
            if(!battery_use) {battery_buffer.started=false;battery_buffer.active=false;battery_buffer.remaining_ms=0;}
            if(settings.zero_feed_enabled && fresh(now_ms(),house_power_at,house_meter_ttl()) && fresh(now_ms(),actual_at,wallbox_meter_ttl())) {
                int64_t now=now_ms();
                settings.pv_surplus_a=pv_available_current_locked(now,house_power_w);
                pv_at=now_ms(); next.pv_surplus_a=settings.pv_surplus_a;
                /* Recalculate availability immediately, but let the regular
                   PV controller ramp down and apply its 30-second deficit
                   grace period. A preference toggle is not an emergency stop.
                   SOC limits and stale telemetry still block independently. */
            }
        }

        if(next.enabled!=settings.enabled){memset(&http_regulator,0,sizeof(http_regulator));memset(&manual_filter,0,sizeof(manual_filter));}
        settings.mode=next.mode; settings.enabled=next.enabled;

        settings.manual_current_a=next.manual_current_a; settings.manual_phases=next.manual_phases; settings.pv_surplus_a=next.pv_surplus_a;
        if(transition)mode_transition_locked(previous_mode,previous_target,change_at);
        if(user_override && charge_plan.active){charge_plan.active=false;plan_phase=PLAN_OFF;plan_dirty=true;plan_revision++;event_locked("plan_cancelled");}
        if(user_override)event_locked(next.enabled?"control_changed":"user_stop");

        if(pv) pv_at=now_ms();

    }

    else valid=false;

    UNLOCK();
    if(valid && persist_preferences) {
        if(xSemaphoreTake(maintenance_flash_lock,ticks(2000))==pdTRUE){valid=save_control_memory(true);xSemaphoreGive(maintenance_flash_lock);}
        else valid=false;
    }
    return valid;

}
