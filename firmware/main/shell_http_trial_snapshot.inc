/* Small private test snapshot. Avoid the full dashboard's settings/UI trees
   while the test client observes control. GET never advances a controller. */
static cJSON *shell_http_trial_snapshot(void){
    cJSON *o=cJSON_CreateObject();if(!o)return NULL;
    tcp_pcb_diag_snapshot_t tcp=tcp_pcb_diag_read();LOCK();int64_t now=now_ms();
#define N(k,v) cJSON_AddNumberToObject(o,k,v)
#define B(k,v) cJSON_AddBoolToObject(o,k,v)
#define S(k,v) cJSON_AddStringToObject(o,k,v)
    S("token",csrf_token);S("build_id",EMS_BUILD_ID);N("uptime_s",now/1000);
    B("tcp_pcb_diag_available",tcp.available);B("tcp_pcb_diag_pending",tcp.pending);B("tcp_pcb_diag_truncated",tcp.truncated);
    N("tcp_pcb_samples",tcp.samples);N("tcp_pcb_queue_skips",tcp.queue_skips);
    nullable(o,"tcp_pcb_age_ms",now-tcp.at,tcp.samples>0);
    N("tcp_pcb_active",tcp.active);N("tcp_pcb_time_wait",tcp.time_wait);N("tcp_pcb_bound",tcp.bound);N("tcp_pcb_listening",tcp.listening);
    N("tcp_pcb_bytes",tcp.pcb_bytes);N("tcp_pcb_size",tcp.pcb_size);
    N("tcp_tw_incoming_http",tcp.tw_incoming_http);N("tcp_tw_remote_http",tcp.tw_remote_http);
    N("tcp_tw_modbus",tcp.tw_modbus);N("tcp_tw_shell",tcp.tw_shell);N("tcp_tw_other",tcp.tw_other);
    B("enabled",settings.enabled);S("mode",mode_name(settings.mode));
    N("current_a",settings.manual_current_a);N("target_current_a",target_locked(now));
    N("current_offset_a",settings.current_offset_a);
    S("calibration_state",current_calibration.since?"learning":current_calibration.adjustments?"adjusted":"waiting");
    B("calibration_frozen",settings.shell_http_control_enabled||normal_offset_trial.armed||shell_http_trial.armed);
    B("normal_offset_trial_armed",normal_offset_trial.armed);B("normal_offset_trial_fault",normal_offset_trial.fault);
    nullable(o,"original_offset_a",normal_offset_trial.original,normal_offset_trial.armed);
    N("manual_phases",settings.manual_phases);N("charge_phases",settings.charge_phases);
    N("min_charge_a",settings.min_charge_a);N("max_charge_a",settings.max_charge_a);N("grid_limit_a",settings.grid_limit_a);
    B("phase_switch_enabled",settings.phase_switch_enabled);
    S("phase_state",phase_state==PHASE_READY?"bereit":phase_state==PHASE_STOPPING?"stoppen":phase_state==PHASE_MOVING?"schalten":phase_state==PHASE_SETTLING?"pruefen":"fehler");
    N("phase_feedback",phase_gpio_ready?phase_observed():0);
    B("phase_shelly_output",phase_shelly_feedback_value.output);
    B("phase_shelly_config_ok",phase_shelly_config_ok);B("phase_shelly_rechecking",phase_shelly_rechecking);
    N("phase_shelly_transport_misses",phase_shelly_transport_misses);N("phase_shelly_invalid_replies",phase_shelly_invalid_replies);
    nullable(o,"phase_shelly_last_read_failure_age_ms",now-phase_shelly_last_read_failure_at,phase_shelly_last_read_failure_at>0);
    nullable(o,"phase_shelly_failure_http_error",phase_shelly_failure_http_error,phase_shelly_failure_http_valid);
    nullable(o,"phase_shelly_failure_http_status",phase_shelly_failure_http_status,phase_shelly_failure_http_valid);
    nullable(o,"phase_shelly_failure_http_errno",phase_shelly_failure_http_errno,phase_shelly_failure_http_valid);
    nullable(o,"phase_shelly_failure_open_sockets",phase_shelly_failure_open_sockets,phase_shelly_failure_resources_valid);
    nullable(o,"phase_shelly_failure_socket_capacity",phase_shelly_failure_socket_capacity,phase_shelly_failure_resources_valid);
    nullable(o,"phase_shelly_failure_internal_free",phase_shelly_failure_internal_free,phase_shelly_failure_resources_valid);
    nullable(o,"phase_shelly_failure_internal_largest",phase_shelly_failure_internal_largest,phase_shelly_failure_resources_valid);
    nullable(o,"phase_shelly_status_age_ms",now-phase_shelly_at,phase_shelly_at>0);
    nullable(o,"phase_voltage_sample_age_ms",now-phase_voltage.sample_at,phase_voltage.sample_at>0);
    nullable(o,"phase_voltage_proof_age_ms",now-phase_voltage.at,phase_voltage.at>0);
    N("phase_voltage_position",phase_voltage_observed_locked(now));N("phase_voltage_samples",phase_voltage.samples);
    json_add_owned_to_object(o,"phase_voltage_v",number_array(phase_voltage.voltage,3,phase_voltage.sample_at>0&&fresh(now,phase_voltage.sample_at,PHASE_VOLTAGE_TTL_MS)));
    bool actual_ok=feedback_ready(now)&&fresh(now,actual_at,wallbox_meter_ttl());
    B("meter_ok",feedback_ready(now)&&fresh(now,meter_at,wallbox_meter_ttl()));
    B("wallbox_ok",fresh(now,wallbox_at,ems_param_ms(EP_SHELL_TTL)));B("feedback_ok",actual_ok);
    B("storage_ok",stats_storage_ok&&config_storage_ok&&!storage_error&&plan_storage_ok&&control_storage_ok);
    B("runtime_watchdog_failed",runtime_watchdog_failed);B("firmware_check_ok",!boot_validation_pending&&!boot_validation_failed);
    B("firmware_check_pending",boot_validation_pending);B("reboot_required",reboot_required||expert_reboot_pending);
    B("charge_stop_latched",charge_guard.latched);S("block_reason",block_reason_locked(now));
    bool known=shell_vehicle_fresh(&evcc_shell,evcc_shell_at,now);
    B("vehicle_status_known",known);S("vehicle_connection_state",known?vehicle_state_name(evcc_shell.state):"unknown");
    B("http_control_trial_available",true);B("http_control_trial_armed",shell_http_trial.armed);B("http_control_trial_fault",shell_http_trial.fault);
    S("http_control_trial_stage",shell_http_trial_stage(shell_http_trial.stage));N("http_control_trial_steps",shell_http_trial.steps);
    B("shell_http_control_enabled",settings.shell_http_control_enabled);B("shell_http_control_available",!evcc_control.configured);B("shell_http_control_fault",http_regulator.control.fault);
    S("http_control_regulation_source",(settings.shell_http_control_enabled||shell_http_trial.armed)?"shell_http_pilot":"normal");S("http_control_phase_guard","independent_meter");
    nullable(o,"http_pilot_a",evcc_shell.http_control.pilot_a,evcc_shell.http_control.pilot_valid);
    nullable(o,"http_delayed_current_a",evcc_shell.http_control.current_a,evcc_shell.http_control.current_valid);
    nullable(o,"http_control_sample_age_ms",now-evcc_shell_at,evcc_shell_at>0);
    N("http_sample_sequence",trial_http_sequence);N("uart_send_sequence",trial_uart_sequence);
    B("http_car_charging",evcc_shell.http_control.car_charging);
    N("http_sample_received_ms",evcc_shell_at);N("uart_sent_ms",last_sent_current_at);
    json_add_owned_to_object(o,"actual_a",number_array(actual_a,3,actual_ok));
    json_add_owned_to_object(o,"last_sent_a",number_array(last_sent_a,3,last_sent_current_at>0));
    nullable(o,"last_sent_age_ms",now-last_sent_current_at,last_sent_current_at>0);
    N("free_heap",esp_get_free_heap_size());
    N("minimum_free_heap",esp_get_minimum_free_heap_size());
    N("largest_free_block",heap_caps_get_largest_free_block(MALLOC_CAP_8BIT));
    http_display_status(o,now);
#undef N
#undef B
#undef S
    UNLOCK();return o;
}
