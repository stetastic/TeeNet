static bool ota_install_allowed_locked(int64_t now){
    return !restarting&&!ota_in_progress&&!backup_in_progress&&!evcc_configuring&&
        !boot_validation_pending&&!phase_test_active&&phase_config_gate_locked(now)==CONFIG_GATE_OK;
}
