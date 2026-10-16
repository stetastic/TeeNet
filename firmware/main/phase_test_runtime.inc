/* Deliberate hardware test. No charge request and no direct relay writes here. */
static bool phase_test_zero_locked(int64_t now){
    float limit=fminf(1,ems_param(EP_PHASE_ZERO_A));
    return fresh(now,actual_at,wallbox_meter_ttl())&&
        isfinite(actual_a[0])&&actual_a[0]>=0&&actual_a[0]<limit&&
        isfinite(actual_a[1])&&actual_a[1]>=0&&actual_a[1]<limit&&
        isfinite(actual_a[2])&&actual_a[2]>=0&&actual_a[2]<limit&&
        (settings.phase_actuator_type==0||phase_measured_zero(&settings,actual_a,limit));
}
static const char *phase_test_block_locked(int64_t now){
    if(phase_test_active)return "Ein Schuetztest laeuft bereits.";
    if(!settings.phase_switch_enabled)return "Phasenumschaltung zuerst einrichten und speichern.";
    if(phase_config_saving)return "Einstellungen werden gerade gespeichert.";
    if(restart_pending())return "Erforderlichen Neustart und Firmware-Startpruefung abschliessen.";
    if(restarting||ota_in_progress||backup_in_progress||evcc_configuring)return "Wartung laeuft. Danach erneut versuchen.";
    if(settings.enabled||evcc_control.enabled)return "Ladeanforderung noch aktiv. Zuerst Stoppen druecken.";
    if(!fresh(now,actual_at,wallbox_meter_ttl()))return "Keine aktuelle Wallbox-Strommessung. Zaehlermessung pruefen.";
    for(unsigned p=0;p<3;p++){
        if(!isfinite(actual_a[p])||actual_a[p]<0)return "Wallbox-Strommessung ungueltig.";
        if(actual_a[p]>=1)return "Mindestens eine Phase hat 1 A oder mehr. Stromstillstand abwarten.";
    }
    if(!phase_test_zero_locked(now))return "Stromstillstand noch nicht bestaetigt. Stromwerte und gemessene Phasen pruefen.";
    if(!phase_gpio_ready||!phase_config_quiescent_locked())return "Schaltvorgang oder Schnittstelle noch nicht bereit.";
    if(phase_mqtt_remote())return phase_mqtt_allowed&&phase_mqtt_supported_locked()&&phase_output_link_locked()?"":"Externe Schuetzsteuerung nicht freigegeben oder nicht verbunden.";
    if(phase_shelly_remote()){
        if(!wifi_online||!phase_shelly_config_ok||!fresh(now,phase_shelly_at,SHELLY_PHASE_TTL_MS))return "Shelly-Verbindung nicht bereit. Adresse, Anmeldung und Shelly-Einstellungen pruefen.";
        if(phase_shelly_uncertain)return "Letzter Shelly-Schaltbefehl unklar. Stellung und Rueckmeldung pruefen.";
        if(settings.phase_shelly_pulse&&!phase_shelly_observed_locked(now))return "Bistabiles Schuetz: beide Rueckmeldekontakte muessen die Stellung eindeutig bestaetigen.";
        return "";
    }
    return relay_ready?"":"GPIO-Schaltausgang nicht bereit.";
}
static bool phase_test_ready_locked(int64_t now){
    return phase_test_block_locked(now)[0]==0;
}
static void phase_test_end_locked(void){
    if(!phase_test_active)return;
    phase_test_active=false;phase_test_until=0;
    settings.enabled=false;settings.mode=MODE_OFF;
    /* Cancel unsent commands. An in-flight write may still finish; do not
       repeat it or claim a confirmed position after cancellation. */
    if(phase_state!=PHASE_READY){
        phase_shelly_pending=false;phase_mqtt_pending=false;phase_mqtt_generation++;
        phase_state=PHASE_FAULT;
    }
}
static void phase_test_tick_locked(int64_t now){
    if(phase_test_active&&(restarting||ota_in_progress||now>=phase_test_until))phase_test_end_locked();
}
static bool phase_fault_restart_safe_locked(int64_t now){
    if((phase_state!=PHASE_FAULT&&!(phase_startup&&phase_config_quiescent_locked()))||settings.enabled||evcc_control.enabled||
       phase_mqtt_inflight||phase_mqtt_pending||phase_shelly_pending||phase_shelly_inflight)return false;
    if(!phase_shelly_remote()&&!phase_mqtt_remote()&&relay_on[0])return false;
    /* A Shelly reboot does not change its output. In a stopped setup a bad
       contact must not prevent rebooting TeeNet to apply corrected wiring. */
    return hardware_config_change_allowed(false,true,actual_at>0||meter_at>0,
        fresh(now,actual_at,wallbox_meter_ttl()),actual_a);
}
