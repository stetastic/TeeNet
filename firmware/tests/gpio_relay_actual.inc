static void relay_set(unsigned index,bool energized) {
    if(settings.phase_actuator_type!=0 || !relay_ready || !settings.relay_board_enabled || index>0) return;
    gpio_set_level(settings.relay1_pin,
        relay_output_level(settings.relay_active_low,energized));
    relay_on[index]=energized;
}

static bool phase_actuator_begin_locked(uint8_t goal,int64_t now){
    if(phase_mqtt_remote())return phase_mqtt_begin_locked(goal,now);
    if(phase_shelly_remote())return phase_shelly_begin_locked(goal,now);
    relay_set(0,goal==1);return true;
}
static void phase_setup(void) {
    settings.charge_phases=settings.phase_switch_enabled?3:settings.fixed_charge_phases; settings.manual_phases=settings.charge_phases;
    if(!settings.relay_board_enabled) return;
    if(phase_mqtt_remote()){phase_mqtt_setup_locked(now_ms());return;}
    if(phase_shelly_remote()){
        relay_ready=phase_gpio_ready=true;
        if(settings.phase_switch_enabled){phase_goal=3;phase_move_at=now_ms();phase_state=PHASE_MOVING;}
        return; /* Never touch GPIO12/13 for the Shelly profile. */
    }
    int off=relay_output_level(settings.relay_active_low,false);
    gpio_set_level(settings.relay1_pin,off);
    gpio_config_t output={.pin_bit_mask=(1ULL<<settings.relay1_pin),.mode=GPIO_MODE_OUTPUT,
        .pull_up_en=settings.relay_active_low?GPIO_PULLUP_ENABLE:GPIO_PULLUP_DISABLE,
        .pull_down_en=settings.relay_active_low?GPIO_PULLDOWN_DISABLE:GPIO_PULLDOWN_ENABLE,.intr_type=GPIO_INTR_DISABLE};
    relay_ready=gpio_config(&output)==ESP_OK;
    relay_set(0,false);
    if(!settings.phase_switch_enabled) return;
    if(!settings.phase_feedback_enabled) phase_gpio_ready=relay_ready;
    else {
        gpio_config_t input={.pin_bit_mask=(1ULL<<PHASE_FEEDBACK_GPIO)|
            (settings.phase_feedback_count==2?(1ULL<<PHASE_FEEDBACK_SECOND_GPIO):0),.mode=GPIO_MODE_INPUT,
            .pull_up_en=GPIO_PULLUP_ENABLE,.pull_down_en=GPIO_PULLDOWN_DISABLE,.intr_type=GPIO_INTR_DISABLE};
        phase_gpio_ready=relay_ready && gpio_config(&input)==ESP_OK;
    }
    if(!phase_gpio_ready) phase_state=PHASE_FAULT;
    else {
        /* Let the deenergized contactor return before evaluating position.
           Startup stays blocked through normal feedback and fresh-data checks. */
        phase_goal=3;phase_move_at=now_ms();phase_state=PHASE_MOVING;
    }
}

