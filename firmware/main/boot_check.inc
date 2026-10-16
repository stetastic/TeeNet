/* A separate temporary observer detects stalled control tasks as well as boot errors. */
static void boot_check_task(void *arg){
    (void)arg;boot_check_t check={.started=(uint32_t)now_ms()};
    for(;;){
        uint32_t now=(uint32_t)now_ms();bool healthy=false;
        if(xSemaphoreTake(state_lock,ticks(100))==pdTRUE){
            healthy=boot_config_ok&&config_storage_ok&&web_started&&wallbox_ready&&meter_ready&&house_bus_ready&&contact_gpio_ready&&
                (!settings.phase_switch_enabled||phase_gpio_ready)&&(!settings.wifi_ssid[0]||wifi_online)&&
                boot_task_seen[0]&&boot_task_seen[1]&&boot_task_seen[2]&&now-boot_task_seen[0]<10000&&
                now-boot_task_seen[1]<30000&&now-boot_task_seen[2]<30000;
            if(settings.zero_feed_enabled||settings.grid_guard_enabled)healthy=healthy&&boot_task_seen[3]&&now-boot_task_seen[3]<30000;
            if(settings.phase_switch_enabled&&settings.phase_actuator_type!=0)healthy=healthy&&boot_task_seen[4]&&now-boot_task_seen[4]<30000;
            UNLOCK();
        }
        int result=boot_check_step(&check,now,healthy);
        if(result==BOOT_CHECK_PASS){
            esp_err_t err=boot_rollback_armed?esp_ota_mark_app_valid_cancel_rollback():ESP_OK;
            if(err==ESP_OK){LOCK();
                if(phase_startup){phase_stop_at=phase_move_at=now_ms();phase_zero_at=0;}
                boot_validation_pending=false;boot_validation_ok=true;event_locked("firmware_verified");UNLOCK();ESP_LOGI(TAG,"Firmware startup check passed");vTaskDelete(NULL);return;}
            ESP_LOGE(TAG,"Unable to confirm OTA image: %s",esp_err_to_name(err));result=BOOT_CHECK_FAIL;
        }
        if(result==BOOT_CHECK_FAIL){
            ESP_LOGE(TAG,"Startup self-test failed; charging remains blocked");
            if(boot_rollback_armed&&esp_ota_check_rollback_is_possible()){
                esp_err_t err=esp_ota_mark_app_invalid_rollback_and_reboot();
                ESP_LOGE(TAG,"OTA rollback did not restart: %s",esp_err_to_name(err));
            }
            /* Keep the recovery web page reachable if no compatible rollback exists. */
            if(xSemaphoreTake(state_lock,ticks(100))==pdTRUE){boot_validation_failed=true;boot_validation_pending=false;settings.enabled=false;event_locked("firmware_check_failed");UNLOCK();}
            else {boot_validation_failed=true;boot_validation_pending=false;}
            vTaskDelete(NULL);return;
        }
        vTaskDelay(ticks(500));
    }
}
