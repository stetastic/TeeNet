/* Access only under the application lock. No network operation in the control loop. */
static shelly_phase_feedback_t phase_shelly_feedback_value;
static int64_t phase_shelly_at,phase_shelly_config_at,phase_shelly_command_at;
static bool phase_shelly_config_ok,phase_shelly_pending,phase_shelly_uncertain,phase_shelly_inflight;
static bool phase_shelly_restart_prepared;
static bool phase_shelly_rechecking;
static int64_t phase_shelly_recheck_at;
static uint32_t phase_shelly_uptime;
static bool phase_shelly_have_uptime;
static uint8_t phase_shelly_goal;
static uint32_t phase_shelly_transport_misses,phase_shelly_invalid_replies;
static int64_t phase_shelly_last_read_failure_at;
static int phase_shelly_http_error,phase_shelly_http_status,phase_shelly_http_errno;
static int phase_shelly_failure_http_error,phase_shelly_failure_http_status,phase_shelly_failure_http_errno;
static bool phase_shelly_http_diag_valid,phase_shelly_failure_http_valid;
static bool phase_shelly_http_resources_valid,phase_shelly_failure_resources_valid;
static unsigned phase_shelly_http_open_sockets,phase_shelly_http_socket_capacity;
static unsigned phase_shelly_failure_open_sockets,phase_shelly_failure_socket_capacity;
static size_t phase_shelly_http_internal_free,phase_shelly_http_internal_largest;
static size_t phase_shelly_failure_internal_free,phase_shelly_failure_internal_largest;
static void phase_shelly_read_failure_locked(bool missing,int64_t now){
    uint32_t *count=missing?&phase_shelly_transport_misses:&phase_shelly_invalid_replies;
    if(*count<UINT32_MAX)(*count)++;
    phase_shelly_last_read_failure_at=now;
    phase_shelly_failure_http_valid=phase_shelly_http_diag_valid;
    phase_shelly_failure_http_error=phase_shelly_http_error;
    phase_shelly_failure_http_status=phase_shelly_http_status;
    phase_shelly_failure_http_errno=phase_shelly_http_errno;
    phase_shelly_failure_resources_valid=phase_shelly_http_resources_valid;
    phase_shelly_failure_open_sockets=phase_shelly_http_open_sockets;
    phase_shelly_failure_socket_capacity=phase_shelly_http_socket_capacity;
    phase_shelly_failure_internal_free=phase_shelly_http_internal_free;
    phase_shelly_failure_internal_largest=phase_shelly_http_internal_largest;
}
static bool phase_shelly_remote(void){return settings.phase_actuator_type==1||settings.phase_actuator_type==3;}
static unsigned phase_shelly_observed_locked(int64_t now){
    if(!phase_shelly_config_ok||!fresh(now,phase_shelly_at,SHELLY_PHASE_TTL_MS))return 0;
    unsigned observed=phase_uses_voltage(&settings)?phase_voltage_observed_locked(now):
        (phase_shelly_feedback_value.output==settings.phase_shelly_true_three_phase?3:1);
    if(phase_shelly_feedback_value.output!=
       (settings.phase_shelly_true_three_phase?observed==3:observed==1))return 0;
    return observed;
}
static bool phase_shelly_begin_locked(uint8_t goal,int64_t now){
    unsigned observed=phase_shelly_observed_locked(now);
    bool test_absolute=phase_test_active&&phase_shelly_config_ok&&fresh(now,phase_shelly_at,SHELLY_PHASE_TTL_MS);
    if((!observed&&!test_absolute)||phase_shelly_uncertain||phase_shelly_pending)return false;
    phase_shelly_goal=goal;phase_shelly_pending=observed!=goal;
    phase_shelly_command_at=0;return true;
}
static bool phase_shelly_motion_complete_locked(unsigned goal,int64_t now){
    return !phase_shelly_pending && !phase_shelly_uncertain &&
        phase_shelly_observed_locked(now)==goal &&
        (!phase_shelly_command_at||(phase_shelly_at>phase_shelly_command_at&&(!phase_uses_voltage(&settings)||phase_voltage.at>phase_shelly_command_at)));
}
static void phase_shelly_lost_locked(void){
    phase_shelly_at=0;phase_shelly_pending=false;phase_shelly_rechecking=false;
    /* The worker starts before Wi-Fi connects. Initial absence is not a lost
       running contactor: the controller gives boot a bounded connection wait. */
    bool boot_wait=phase_startup&&phase_state==PHASE_MOVING&&!phase_shelly_command_at;
    if(settings.phase_switch_enabled&&!boot_wait)phase_state=PHASE_FAULT;
}
/* Unknown data pause the existing position without issuing a switch command.
   Only fresh matching evidence can recover; contradictions and failed writes latch. */
static void phase_shelly_transport_lost_locked(int64_t now){
    phase_shelly_at=0;
    if(settings.phase_switch_enabled&&!phase_shelly_uncertain&&!phase_shelly_inflight&&!phase_shelly_pending&&
       (phase_state==PHASE_READY||(phase_state==PHASE_SETTLING&&phase_shelly_rechecking))){
        if(!phase_shelly_rechecking){phase_shelly_recheck_at=now;phase_goal=settings.charge_phases;}
        phase_shelly_rechecking=true;phase_move_at=now;phase_state=PHASE_SETTLING;
    }else phase_shelly_lost_locked();
}
/* A missing transport response is not new evidence. Keep the last verified
   status only for its original TTL; never update a timestamp or acknowledge a
   command. Invalid payloads/configuration still take the immediate-loss path. */
static inline void phase_shelly_transport_missing_locked(int64_t now){
    phase_shelly_read_failure_locked(true,now);
    if(wifi_online&&phase_shelly_config_ok&&!phase_shelly_uncertain&&
       fresh(now,phase_shelly_at,SHELLY_PHASE_TTL_MS))return;
    phase_shelly_transport_lost_locked(now);
}
static void phase_shelly_guard_locked(int64_t now){
    if(!phase_shelly_remote()||!settings.phase_switch_enabled)return;
    if((restarting||ota_in_progress)&&phase_shelly_pending){
        phase_shelly_pending=false;phase_goal=settings.charge_phases;
    }
    if(!restarting&&!ota_in_progress)phase_shelly_restart_prepared=false;
    unsigned volts=phase_uses_voltage(&settings)?phase_voltage_observed_locked(now):0;
    bool contradiction=volts&&phase_shelly_config_ok&&fresh(now,phase_shelly_at,SHELLY_PHASE_TTL_MS)&&
        phase_shelly_feedback_value.output!=(settings.phase_shelly_true_three_phase?volts==3:volts==1);
    if((phase_state==PHASE_READY||phase_shelly_rechecking)&&contradiction){phase_shelly_rechecking=false;phase_state=PHASE_FAULT;return;}
    if(phase_state==PHASE_READY){
        unsigned observed=phase_shelly_observed_locked(now);
        if(phase_shelly_uncertain||(observed&&observed!=settings.charge_phases))phase_state=PHASE_FAULT;
        else if(!wifi_online||!observed)phase_shelly_transport_lost_locked(now);
    }
    if((restarting||ota_in_progress)&&phase_state==PHASE_READY&&!phase_shelly_restart_prepared){
        /* Stop and verify, but retain a bistable mechanical position. */
        phase_shelly_restart_prepared=true;phase_goal=settings.charge_phases;
        phase_state=PHASE_STOPPING;phase_stop_at=now;phase_zero_at=0;
    }
}
static bool phase_shelly_zero_locked(int64_t now){
    return fresh(now,actual_at,wallbox_meter_ttl()) && actual_at>phase_stop_at &&
        phase_zero_at && actual_at-phase_zero_at>=ems_param_ms(EP_PHASE_ZERO) &&
        phase_measured_zero(&settings,actual_a,phase_test_active?fminf(1,ems_param(EP_PHASE_ZERO_A)):ems_param(EP_PHASE_ZERO_A)) &&
        ((phase_test_active&&!settings.enabled&&!evcc_control.enabled)||
         (last_sent_current_at>phase_stop_at&&fresh(now,wallbox_at,ems_param_ms(EP_SHELL_TTL))));
}
static bool phase_shelly_receive_locked(const shelly_phase_feedback_t *feedback,bool read,int64_t now){
    bool rebooted=read&&feedback->has_uptime&&phase_shelly_have_uptime&&feedback->uptime<phase_shelly_uptime;
    if(!read||rebooted){
        phase_shelly_read_failure_locked(false,now);
        if(rebooted){phase_shelly_config_ok=false;phase_shelly_uncertain=true;phase_shelly_lost_locked();}
        else phase_shelly_transport_lost_locked(now);
    }else{
        phase_shelly_feedback_value=*feedback;phase_shelly_at=now;
        unsigned observed=phase_shelly_observed_locked(now);
        if(observed)relay_on[0]=observed==1;
        if(phase_startup&&phase_state==PHASE_MOVING&&!phase_shelly_pending&&observed){
            phase_goal=(uint8_t)observed;settings.charge_phases=phase_goal;
        }
    }
    if(read&&feedback->has_uptime){phase_shelly_uptime=feedback->uptime;phase_shelly_have_uptime=true;}
    return read&&!rebooted;
}
static bool phase_shelly_take_command_locked(int64_t now){
    if(phase_config_saving||!phase_shelly_pending||phase_state!=PHASE_MOVING||phase_shelly_goal!=phase_goal||
       !phase_shelly_config_ok||phase_shelly_uncertain||!wifi_online||
       restarting||ota_in_progress||!phase_shelly_zero_locked(now)||
       (!phase_shelly_observed_locked(now)&&!(phase_test_active&&fresh(now,phase_shelly_at,SHELLY_PHASE_TTL_MS))))return false;
    phase_shelly_pending=false;
    /* A contact alone cannot confirm an absolute output. During an explicit
       test the contact may already report the target while the output still
       disagrees. Only the fully validated position can make a write redundant. */
    if(phase_shelly_observed_locked(now)==phase_shelly_goal)return false;
    phase_shelly_command_at=now;phase_shelly_inflight=true;return true;
}
static void phase_shelly_ack_locked(bool ok){
    phase_shelly_inflight=false;
    if(!ok){phase_shelly_uncertain=true;phase_shelly_lost_locked();}
}
