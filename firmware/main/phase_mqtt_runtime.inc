/* Outgoing absolute states only. There is deliberately no MQTT phase input.
   Caller holds state_lock; the network task publishes outside that lock. */
static bool phase_mqtt_pending,phase_mqtt_inflight,phase_mqtt_known,phase_mqtt_seen_connection;
static uint8_t phase_mqtt_goal,phase_mqtt_assumed;
static uint32_t phase_mqtt_generation;
static int64_t phase_mqtt_sent_at,phase_output_direct_at,phase_output_health_due;
static bool phase_output_direct_online;
static bool phase_mqtt_remote(void){return settings.phase_actuator_type==2;}
static bool phase_mqtt_supported_locked(void){
    return phase_output_settings_supported(&settings);
}
static bool phase_output_link_locked(void){
    return wifi_online&&(settings.phase_output_transport==0?mqtt_online:
        phase_output_direct_online&&fresh(now_ms(),phase_output_direct_at,5000));
}
static bool phase_mqtt_stopped_locked(void){
    return !settings.enabled&&!evcc_control.enabled&&!phase_mqtt_inflight&&!restarting&&
        !ota_in_progress&&!backup_in_progress&&!evcc_configuring&&
        (phase_state==PHASE_READY||phase_state==PHASE_FAULT)&&
        hardware_config_change_allowed(false,true,actual_at>0||meter_at>0,
            fresh(now_ms(),actual_at,wallbox_meter_ttl()),actual_a);
}
static void phase_mqtt_cancel_locked(bool stop){
    phase_mqtt_pending=false;phase_mqtt_known=false;phase_mqtt_generation++;
    if(phase_mqtt_remote()){
        phase_state=PHASE_FAULT;
        if(stop){settings.enabled=false;settings.mode=MODE_OFF;evcc_control.enabled=false;evcc_control.preparing=false;}
    }
}
static void phase_mqtt_setup_locked(int64_t now){
    phase_mqtt_pending=phase_mqtt_inflight=phase_mqtt_known=phase_mqtt_seen_connection=false;
    phase_mqtt_sent_at=0;phase_mqtt_generation++;relay_ready=phase_gpio_ready=true;
    phase_output_direct_online=false;phase_output_direct_at=0;
    phase_output_health_due=0;
    phase_goal=3;phase_startup=true;phase_stop_at=now;phase_zero_at=0;
    phase_state=phase_mqtt_allowed&&phase_mqtt_supported_locked()?PHASE_STOPPING:PHASE_FAULT;
    /* No startup command: first confirm the zero-current stop interval. */
}
static void phase_mqtt_guard_locked(void){
    if(!phase_mqtt_remote()||!settings.phase_switch_enabled)return;
    if(phase_output_link_locked())phase_mqtt_seen_connection=true;
    if(!phase_mqtt_allowed||!phase_mqtt_supported_locked()||
       (!phase_output_link_locked()&&phase_mqtt_seen_connection)){
        settings.enabled=false;settings.mode=MODE_OFF;evcc_control.enabled=false;evcc_control.preparing=false;
        if(phase_state!=PHASE_FAULT||phase_mqtt_pending||phase_mqtt_known)phase_mqtt_cancel_locked(true);
        return;
    }
    if(phase_mqtt_pending&&phase_mqtt_goal==1&&(restarting||ota_in_progress)){
        phase_mqtt_pending=false;phase_goal=3;phase_state=PHASE_STOPPING;phase_stop_at=now_ms();phase_zero_at=0;
    }
}
static bool phase_mqtt_zero_locked(int64_t now){
    float limit=phase_test_active?fminf(1,ems_param(EP_PHASE_ZERO_A)):ems_param(EP_PHASE_ZERO_A);
    return phase_zero_at&&fresh(now,actual_at,wallbox_meter_ttl())&&actual_at>phase_stop_at&&
        actual_at-phase_zero_at>=ems_param_ms(EP_PHASE_ZERO)&&
        phase_measured_zero(&settings,actual_a,limit)&&
        ((phase_test_active&&!settings.enabled&&!evcc_control.enabled)||
         (last_sent_current_at>phase_stop_at&&fresh(now,wallbox_at,ems_param_ms(EP_SHELL_TTL))));
}
static bool phase_mqtt_begin_locked(uint8_t goal,int64_t now){
    if(!phase_mqtt_remote()||!phase_mqtt_allowed||phase_mqtt_saving||
       !phase_mqtt_supported_locked()||!phase_output_link_locked()||
       phase_mqtt_pending||phase_mqtt_inflight||!phase_mqtt_zero_locked(now)||(goal!=1&&goal!=3))return false;
    phase_mqtt_goal=goal;phase_mqtt_sent_at=0;
    phase_mqtt_pending=!phase_mqtt_known||phase_mqtt_assumed!=goal;
    if(!phase_mqtt_pending)phase_mqtt_sent_at=now;
    return true;
}
static bool phase_mqtt_take_locked(int64_t now,uint8_t *goal,uint32_t *generation){
    if(phase_config_saving||!phase_mqtt_pending||phase_state!=PHASE_MOVING||phase_mqtt_goal!=phase_goal||
       !phase_mqtt_allowed||phase_mqtt_saving||!phase_output_link_locked()||
       !phase_mqtt_supported_locked()||!phase_mqtt_zero_locked(now)||
       (phase_mqtt_goal==1&&(restarting||ota_in_progress)))return false;
    phase_mqtt_pending=false;phase_mqtt_inflight=true;
    *goal=phase_mqtt_goal;*generation=phase_mqtt_generation;return true;
}
static void phase_mqtt_sent_locked(uint8_t goal,uint32_t generation,bool success,int64_t now){
    phase_mqtt_inflight=false;
    if(generation!=phase_mqtt_generation||!phase_mqtt_allowed)return;
    if(!success||!phase_output_link_locked()){phase_mqtt_cancel_locked(true);return;}
    phase_mqtt_known=true;phase_mqtt_assumed=goal;phase_mqtt_sent_at=now;relay_on[0]=goal==1;
}
static bool phase_mqtt_motion_complete_locked(uint8_t goal,int64_t now){
    return phase_mqtt_allowed&&phase_output_link_locked()&&phase_mqtt_known&&!phase_mqtt_pending&&!phase_mqtt_inflight&&
        phase_mqtt_assumed==goal&&phase_mqtt_sent_at&&now-phase_mqtt_sent_at>=2000;
}
static void phase_output_health_locked(bool ok,bool on,int64_t now){
    phase_output_direct_online=ok;phase_output_direct_at=ok?now:0;
    if(ok)phase_mqtt_seen_connection=true;
    if(!phase_mqtt_allowed)return; /* Read-only health never restores a phase claim. */
    bool initial_wait=phase_startup&&!phase_mqtt_seen_connection&&phase_state==PHASE_STOPPING;
    if((!ok&&!initial_wait)||(phase_mqtt_known&&now-phase_mqtt_sent_at>=2000&&
             on!=phase_mqtt_output_value(&settings,phase_mqtt_assumed)))phase_mqtt_cancel_locked(true);
}
