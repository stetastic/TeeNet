static uint8_t phase_observed(void) {
    if(phase_shelly_remote())return (uint8_t)phase_shelly_observed_locked(now_ms());
    if(phase_uses_voltage(&settings))return (uint8_t)phase_voltage_observed_locked(now_ms());
    return 0; /* No auxiliary contacts: only optional downstream voltage is observed. */
}

