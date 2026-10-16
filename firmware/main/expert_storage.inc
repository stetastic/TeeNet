static bool expert_stopped_locked(void) {
    return !phase_test_active && !boot_validation_pending && !settings.enabled && !ota_in_progress && !restarting && !backup_in_progress &&
        !evcc_configuring && !(evcc_control.configured&&evcc_control.enabled) && phase_state==PHASE_READY &&
        fresh(now_ms(),actual_at,wallbox_meter_ttl()) && actual_a[0]<1 && actual_a[1]<1 && actual_a[2]<1;
}

/* Recheck the stop after acquiring the flash lock and reserve it atomically.
   MQTT and evcc must not start a charge between validation and the NVS commit. */
static esp_err_t save_expert_values(const expert_values_t *next) {
    if(xSemaphoreTake(maintenance_flash_lock,ticks(2000))!=pdTRUE)return ESP_ERR_TIMEOUT;
    LOCK();
    if(!expert_stopped_locked()){
        UNLOCK();xSemaphoreGive(maintenance_flash_lock);return ESP_ERR_INVALID_STATE;
    }
    bool previous_pending=expert_reboot_pending;
    bool changed=memcmp(next->values,expert_saved.values,sizeof(next->values))!=0;
    if(changed)expert_reboot_pending=true;
    UNLOCK();
    esp_err_t err=ESP_OK;
    if(changed){
        nvs_handle_t handle;err=config_storage_ok?nvs_open("expert",NVS_READWRITE,&handle):ESP_FAIL;
        if(err==ESP_OK){err=nvs_set_blob(handle,"values",next,sizeof(*next));if(err==ESP_OK)err=nvs_commit(handle);nvs_close(handle);}
    }
    LOCK();
    if(err==ESP_OK){
        expert_saved=*next;expert_reboot_pending=false;
        for(unsigned p=0;p<EP_COUNT;p++)expert_reboot_pending|=fabsf(expert_saved.values[p]-ems_param(p))>.00001f;
    }else expert_reboot_pending=previous_pending;
    UNLOCK();xSemaphoreGive(maintenance_flash_lock);return err;
}
