static void phase_step_locked(int64_t now) {
    if(phase_config_saving||(reboot_required&&!restarting&&!ota_in_progress))return;
    phase_mqtt_guard_locked();
    phase_shelly_guard_locked(now);
    if(!settings.phase_switch_enabled) return;
    if(phase_test_active&&phase_state==PHASE_FAULT)phase_test_end_locked();
    if(!phase_gpio_ready) { phase_state=PHASE_FAULT; return; }
    uint8_t observed=phase_observed();
    if(phase_state==PHASE_FAULT) return;
    /* Network components cannot be ready before Wi-Fi. During boot stay
       blocked without a command; never reuse the short mechanical timeout. */
    if(phase_startup&&phase_shelly_remote()&&phase_state==PHASE_MOVING&&!observed){
        if(now-phase_move_at>ems_param_ms(EP_PHASE_STOP_MAX))phase_state=PHASE_FAULT;
        return;
    }
    if(phase_startup&&phase_mqtt_remote()&&phase_state==PHASE_STOPPING&&!phase_output_link_locked()){
        if(now-phase_stop_at>ems_param_ms(EP_PHASE_STOP_MAX))phase_state=PHASE_FAULT;
        return;
    }
    if(phase_state==PHASE_READY && !phase_feedback_matches(settings.phase_feedback_enabled,observed,settings.charge_phases)) { phase_state=PHASE_FAULT; return; }
    if(phase_state==PHASE_READY && settings.charge_phases==1 &&
       fresh(now,actual_at,wallbox_meter_ttl())) {
        float peak=fmaxf(actual_a[0],fmaxf(actual_a[1],actual_a[2]));
        if(peak>=1) {
            phase_no_load_since=0;
        } else {
            if(phase_attempt_at>0) {
                if(!phase_no_load_since) phase_no_load_since=now;
                if(now-phase_no_load_since>=ems_param_ms(EP_PHASE_IDLE)) {
                    /* Without a vehicle-presence signal, stop retrying after
                       one bounded attempt. Keep the coil off until a new
                       explicit Start or a changed command arrives. */
                    phase_idle_inhibit=true; phase_goal=3;
                    if(evcc_control.configured){evcc_control.enabled=false;evcc_control.preparing=false;evcc_control.inhibited=true;settings.enabled=false;settings.mode=MODE_OFF;}
                    phase_state=PHASE_STOPPING; phase_stop_at=now; phase_zero_at=0;
                    return;
                }
            }
        }
    } else if(phase_state==PHASE_READY) {
        phase_no_load_since=0;
    }
    if(phase_state==PHASE_STOPPING) {
        if(phase_goal==1 && (((restarting || ota_in_progress)&&!phase_shelly_remote()) ||
           (!phase_test_active && evcc_control.configured && evcc_desired_phases(&evcc_control,settings.charge_phases)==3)))phase_goal=3;
        bool fresh_meter=fresh(now,actual_at,wallbox_meter_ttl());
        float peak=fmaxf(actual_a[0],fmaxf(actual_a[1],actual_a[2]));
        float zero_limit=phase_test_active?fminf(1,ems_param(EP_PHASE_ZERO_A)):ems_param(EP_PHASE_ZERO_A);
        /* An explicit stopped hardware test needs live current proof, not a
           working Shell data bus. Automatic switching still requires its stop telegram. */
        bool test_stopped=phase_test_active&&!settings.enabled&&!evcc_control.enabled;
        bool zero=fresh_meter && actual_at>phase_stop_at && peak<zero_limit &&
            isfinite(actual_a[0])&&actual_a[0]>=0&&isfinite(actual_a[1])&&actual_a[1]>=0&&isfinite(actual_a[2])&&actual_a[2]>=0&&
            (test_stopped||(last_sent_current_at>phase_stop_at&&fresh(now,wallbox_at,ems_param_ms(EP_SHELL_TTL))));
        if(settings.phase_actuator_type!=0)zero=zero&&phase_measured_zero(&settings,actual_a,zero_limit);
        if(!zero) phase_zero_at=0;
        else if(!phase_zero_at) phase_zero_at=actual_at;
        /* Network samples may remain valid longer than the zero hold. A new
           post-stop measurement must confirm its end; one cached zero is not
           evidence of continuous current-free operation. */
        if(phase_zero_at && actual_at-phase_zero_at>=ems_param_ms(EP_PHASE_ZERO)) {
            if(!phase_actuator_begin_locked(phase_goal,now)){phase_state=PHASE_FAULT;return;}
            phase_move_at=now; phase_state=PHASE_MOVING;
        } else if(now-phase_stop_at>ems_param_ms(EP_PHASE_STOP_MAX)) phase_state=PHASE_FAULT;
        return;
    }
    if(phase_state==PHASE_MOVING) {
        if(phase_mqtt_remote()?phase_mqtt_motion_complete_locked(phase_goal,now):
           phase_shelly_remote()?phase_shelly_motion_complete_locked(phase_goal,now):
           phase_motion_complete(settings.phase_feedback_enabled,observed,phase_goal,now-phase_move_at)) {
            settings.charge_phases=phase_goal;
            phase_move_at=now; phase_state=PHASE_SETTLING;
            phase_attempt_at=phase_no_load_since=0;
            memset(&pv_control,0,sizeof(pv_control));
            memset(&manual_filter,0,sizeof(manual_filter));
        } else if(now-phase_move_at>ems_param_ms(EP_PHASE_FEEDBACK)) phase_state=PHASE_FAULT;
        return;
    }
    if(phase_state==PHASE_SETTLING) {
        if(!phase_feedback_matches(settings.phase_feedback_enabled,observed,phase_goal)) { phase_state=PHASE_FAULT; return; }
        if(now-phase_move_at>=ems_param_ms(EP_PHASE_SETTLE) && meter_at>phase_move_at && (wallbox_at>phase_move_at||
           (phase_test_active&&!settings.enabled&&!evcc_control.enabled&&fresh(now,actual_at,wallbox_meter_ttl())&&actual_at>phase_move_at))) {
            phase_state=PHASE_READY;
            /* Bootstrap and a completed explicit Stop are not PV oscillations.
               The dwell protects ongoing automatic changes, not an idle start. */
            phase_hold_until=phase_startup||!settings.enabled?0:now+ems_param_ms(EP_PHASE_HOLD);
            phase_startup=false;
            if(evcc_control.configured && phase_goal==1 && evcc_control.preparing)phase_attempt_at=now;
            phase_candidate_at=0; phase_candidate=phase_goal;
        } else if(now-phase_move_at>ems_param_ms(EP_PHASE_DATA_MAX)) phase_state=PHASE_FAULT;
        return;
    }
    uint8_t desired=phase_shelly_remote()?settings.charge_phases:3;
    if(restarting || ota_in_progress)desired=phase_shelly_remote()?settings.charge_phases:3;
    else if(evcc_control.configured)desired=(uint8_t)evcc_desired_phases(&evcc_control,settings.charge_phases);
    else if(settings.enabled && settings.mode==MODE_MANUAL) desired=settings.manual_phases;
    else if(settings.enabled && settings.mode==MODE_PV && settings.zero_feed_enabled) {
        float surplus=phase_surplus_w_locked(now);
        float down=settings.min_charge_a*3*settings.nominal_v*settings.power_factor-300;
        float up=fmaxf(6300,down+800);
        /* A 6 kW vehicle-priority ceiling must not prevent return to the
           tested 6 kW three-phase range after the two-minute dwell. */
        float three_phase_min=settings.min_charge_a*3*settings.nominal_v;
        if(settings.pv_allocation_enabled && settings.pv_priority==1 &&
           settings.pv_car_priority_w>=three_phase_min-5)
            up=fminf(up,fmaxf(three_phase_min,settings.pv_car_priority_w));
        if(isfinite(surplus)) {
            if(settings.charge_phases==3 && surplus<down && surplus>=settings.min_charge_a*settings.nominal_v) desired=1;
            else if(settings.charge_phases==1 && surplus+5>=up) desired=3;
            else desired=settings.charge_phases;
        } else desired=settings.charge_phases;
    }
    if(phase_test_active)desired=phase_test_goal;
    if(phase_idle_inhibit && desired==1) desired=3;
    /* Anti-cycling waits apply to active PV operation. Explicit Stop must
       return to the deenergized contactor after the current-zero interlock. */
    bool automatic_change=!restarting && !ota_in_progress && settings.enabled && settings.mode==MODE_PV;
    if(desired==settings.charge_phases ||
       (automatic_change && now<phase_hold_until)) { phase_candidate_at=0; phase_candidate=desired; return; }
    if(phase_candidate!=desired || !phase_candidate_at) { phase_candidate=desired; phase_candidate_at=now; return; }
    /* An idle initial phase choice is validated by the PV start timer after
       switching. Do not stack a second dwell in front of that validation. */
    int64_t dwell=automatic_change&&pv_control.target_a>0?ems_param_ms(desired==1?EP_PHASE_DOWN:EP_PHASE_UP):0;
    if(now-phase_candidate_at<dwell) return;
    phase_goal=desired; phase_state=PHASE_STOPPING; phase_stop_at=now; phase_zero_at=0;
}
