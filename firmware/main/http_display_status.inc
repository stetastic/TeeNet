/* Caller holds state_lock. Stateless rendering does not advance the trial. */
static void http_display_status(cJSON *o,int64_t now){
    bool active=settings.shell_http_control_enabled,fault=http_regulator.control.fault;
#if CONFIG_TEE_HTTP_CONTROL_TRIAL
    active=active||shell_http_trial.armed;if(shell_http_trial.armed)fault=shell_http_trial.fault;
#endif
    cJSON_AddStringToObject(o,"wallbox_estimate_source",active?"http_pilot":"none");
    float voltage[3],previous=0;for(unsigned p=0;p<3;p++){float sum=ems_phase_voltage_sum(&settings,p+1);voltage[p]=sum-previous;previous=sum;}
    http_display_estimate_t estimate=http_display_estimate(&evcc_shell.http_control,evcc_shell_at,now,
        settings.enabled,fault,target_locked(now),settings.charge_phases,
        SHELL_HTTP_DISPLAY_CURRENT_BIAS_A,voltage,settings.power_factor);
    cJSON_AddBoolToObject(o,"wallbox_estimate_valid",active&&estimate.valid);
    json_add_owned_to_object(o,"wallbox_estimated_a",number_array(estimate.current,3,active&&estimate.valid));
    nullable(o,"wallbox_estimated_w",estimate.watts,active&&estimate.valid);
    nullable(o,"wallbox_estimate_age_ms",now-evcc_shell_at,active&&evcc_shell_at>0);
    cJSON_AddStringToObject(o,"wallbox_estimate_voltage_source",ems_phase_voltage_active()?"measured":"nominal");
}
