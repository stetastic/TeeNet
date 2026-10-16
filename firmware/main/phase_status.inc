/* Use the same active parameters as the phase state machine. */
static void phase_wait_status_locked(int64_t now,const char **kind,int64_t *deadline) {
    *kind="none";*deadline=0;
    if(!settings.phase_switch_enabled)return;
    if(phase_startup&&((phase_shelly_remote()&&phase_state==PHASE_MOVING)||
       (phase_mqtt_remote()&&phase_state==PHASE_STOPPING&&!phase_output_link_locked()))){
        *kind="connection";*deadline=(phase_state==PHASE_MOVING?phase_move_at:phase_stop_at)+ems_param_ms(EP_PHASE_STOP_MAX);
        return;
    }
    if(phase_state==PHASE_STOPPING){
        bool confirming=phase_zero_at && now<phase_zero_at+ems_param_ms(EP_PHASE_ZERO);
        *kind=confirming?"zero_hold":phase_zero_at?"fresh_data":"current_zero";
        *deadline=confirming?phase_zero_at+ems_param_ms(EP_PHASE_ZERO):phase_stop_at+ems_param_ms(EP_PHASE_STOP_MAX);
    }else if(phase_state==PHASE_MOVING){
        *kind=settings.phase_feedback_enabled?"contact":"motion";
        *deadline=phase_move_at+(settings.phase_feedback_enabled?ems_param_ms(EP_PHASE_FEEDBACK):2000);
    }else if(phase_state==PHASE_SETTLING){
        bool settling=now<phase_move_at+ems_param_ms(EP_PHASE_SETTLE);
        *kind=settling?"settle":"fresh_data";
        *deadline=phase_move_at+ems_param_ms(settling?EP_PHASE_SETTLE:EP_PHASE_DATA_MAX);
    }else if(settings.enabled && settings.mode==MODE_PV && phase_state==PHASE_READY){
        if(phase_hold_until>now){*kind="hold";*deadline=phase_hold_until;}
        else if(phase_candidate_at>0){
            *kind="candidate";*deadline=phase_candidate_at+ems_param_ms(phase_candidate==1?EP_PHASE_DOWN:EP_PHASE_UP);
        }
    }
}
