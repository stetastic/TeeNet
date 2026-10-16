/* Distinguish reading the initial position from issuing a movement. */
static bool phase_config_quiescent_locked(void){
    if(phase_config_saving||phase_test_active||phase_shelly_pending||phase_shelly_inflight||
       phase_mqtt_pending||phase_mqtt_inflight)return false;
    if(phase_state==PHASE_READY||phase_state==PHASE_FAULT)return true;
    if(!phase_startup)return false;
    if(phase_shelly_remote())return !phase_shelly_command_at&&
        (phase_state==PHASE_MOVING||phase_state==PHASE_SETTLING);
    if(phase_mqtt_remote())return phase_state==PHASE_STOPPING&&!phase_mqtt_known&&!phase_mqtt_sent_at;
    return !relay_on[0]&&(phase_state==PHASE_MOVING||phase_state==PHASE_SETTLING);
}
static config_gate_t phase_config_gate_locked(int64_t now){
    return hardware_config_gate(settings.enabled||(evcc_control.configured&&evcc_control.enabled&&!evcc_control.inhibited),
        phase_config_quiescent_locked(),actual_at>0||meter_at>0,
        fresh(now,actual_at,wallbox_meter_ttl()),actual_a);
}
static const char *phase_config_gate_message(config_gate_t gate){
    switch(gate){
    case CONFIG_GATE_CHARGING:return "Ladeanforderung noch aktiv. Ladung zuerst mit Stoppen beenden.";
    case CONFIG_GATE_MOVING:return "Schaltvorgang oder Schuetztest laeuft. Erst nach dessen Ende speichern.";
    case CONFIG_GATE_STALE:return "Wallbox-Stromwerte sind veraltet. Auf eine neue Messung warten; angezeigte alte Werte reichen nicht.";
    case CONFIG_GATE_INVALID:return "Wallbox-Strommessung ist ungueltig. Zaehlermessung pruefen.";
    case CONFIG_GATE_CURRENT:return "Mindestens eine Phase hat 1 A oder mehr. Stromstillstand abwarten.";
    default:return "";
    }
}
