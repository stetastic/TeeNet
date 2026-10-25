static uint8_t phase_observed(void) {
    if(phase_shelly_remote())return (uint8_t)phase_shelly_observed_locked(now_ms());
    return settings.phase_feedback_enabled?(uint8_t)phase_feedback_contacts(gpio_get_level(PHASE_FEEDBACK_GPIO)==0,
        settings.phase_feedback_count==2&&gpio_get_level(PHASE_FEEDBACK_SECOND_GPIO)==0,
        settings.phase_feedback_count,settings.phase_feedback_closed_is_single):0;
}

