/* New outgoing permission is stored in settings, not the retired mqtt_phases
   key. Revocation cancels jobs immediately; serialize any committed publish. */
static esp_err_t save_phase_mqtt_permission(bool enabled,const char *topic,bool true_three_phase,
                                         uint8_t transport,const char *host,uint8_t switch_id,const char *password){
    LOCK();if(!enabled){phase_mqtt_allowed=false;phase_mqtt_cancel_locked(true);}UNLOCK();
    if(!config_storage_ok)return ESP_FAIL;
    if(xSemaphoreTake(phase_mqtt_send_lock,ticks(4000))!=pdTRUE)return ESP_ERR_TIMEOUT;
    if(xSemaphoreTake(maintenance_flash_lock,ticks(2000))!=pdTRUE){xSemaphoreGive(phase_mqtt_send_lock);return ESP_ERR_TIMEOUT;}
    LOCK();
    settings_t next=saved_settings;
    if(enabled){
        next.phase_actuator_type=2;next.phase_switch_enabled=next.relay_board_enabled=true;
        next.relay1_mode=3;next.phase_feedback_enabled=false;next.basic_mode=false;
        snprintf(next.phase_mqtt_topic,sizeof(next.phase_mqtt_topic),"%s",topic?topic:"");
        next.phase_mqtt_true_three_phase=true_three_phase;
        next.phase_output_transport=transport;next.phase_output_switch_id=switch_id;
        if(transport!=3||strcmp(next.phase_output_host,host?host:""))next.phase_output_password[0]=0;
        if(transport==3&&password&&password[0])snprintf(next.phase_output_password,sizeof(next.phase_output_password),"%s",password);
        snprintf(next.phase_output_host,sizeof(next.phase_output_host),"%s",host?host:"");
    }
    next.phase_mqtt_output_enabled=enabled;
    if(enabled&&(phase_test_active||!phase_mqtt_stopped_locked()||restart_pending()||!wifi_online||
       (transport==0&&!mqtt_online)||!settings_valid(&next)||
       (phase_mqtt_allowed&&(settings.phase_mqtt_true_three_phase!=true_three_phase||
        settings.phase_output_transport!=transport||settings.phase_output_switch_id!=switch_id||
        strcmp(settings.phase_output_host,next.phase_output_host)||strcmp(settings.phase_output_password,next.phase_output_password))))){
        UNLOCK();xSemaphoreGive(maintenance_flash_lock);xSemaphoreGive(phase_mqtt_send_lock);return ESP_ERR_INVALID_STATE;
    }
    phase_mqtt_saving=true;
    UNLOCK();
    esp_err_t err=save_config(&next);
    LOCK();
    if(err==ESP_OK){
        bool applied=settings_apply_live(&settings,&saved_settings,&next);
        saved_settings=next;phase_mqtt_allowed=enabled;
        if(!applied){reboot_required=true;settings.enabled=false;settings.mode=MODE_OFF;}
        if(!enabled){settings.phase_mqtt_output_enabled=false;phase_mqtt_cancel_locked(true);}
        else if(applied&&phase_mqtt_remote()){settings.enabled=false;settings.mode=MODE_OFF;phase_mqtt_setup_locked(now_ms());}
    }
    phase_mqtt_saving=false;
    UNLOCK();xSemaphoreGive(maintenance_flash_lock);xSemaphoreGive(phase_mqtt_send_lock);return err;
}
