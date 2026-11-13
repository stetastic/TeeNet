static meter_hold_t wallbox_meter_hold;
static bool wallbox_meter_hold_locked(int64_t now) {
    if(settings.shell_http_control_enabled)return false;
    return meter_hold_active(&wallbox_meter_hold,&settings,network_wallbox_meter(),
        meter_ready&&wallbox_ready&&(!settings.phase_switch_enabled||phase_state==PHASE_READY),now,actual_at);
}
static bool wallbox_control_ready_locked(int64_t now) {
    if(settings.shell_http_control_enabled)return http_regulation_ready_locked(now);
    return wallbox_meter_hold_locked(now)||(feedback_ready(now)&&fresh(now,meter_at,wallbox_meter_ttl())&&fresh(now,actual_at,wallbox_meter_ttl()));
}
