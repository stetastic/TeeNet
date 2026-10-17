/* Startup validation is local: a router or Internet outage is not a broken
   firmware image. The same observer remains active after validation and
   latches local task stalls without forcing a reset during phase movement. */
static uint32_t runtime_required_tasks_locked(void){
    uint32_t required=RUNTIME_TASK_STATUS|RUNTIME_TASK_WALLBOX|RUNTIME_TASK_METER;
    if(settings.zero_feed_enabled||settings.grid_guard_enabled)required|=RUNTIME_TASK_HOUSE;
    if(settings.phase_switch_enabled&&settings.phase_actuator_type!=0)required|=RUNTIME_TASK_PHASE;
    return required;
}
static void boot_check_task(void *arg){
    (void)arg;boot_check_t check={.started=(uint32_t)now_ms()};runtime_check_t runtime={0};
    uint32_t required=RUNTIME_TASK_STATUS|RUNTIME_TASK_WALLBOX|RUNTIME_TASK_METER;
    bool validated=false,runtime_fault_recorded=false;
    for(;;){
        uint32_t now=(uint32_t)now_ms();bool healthy=false;
        if(xSemaphoreTake(state_lock,ticks(100))==pdTRUE){
            required=runtime_required_tasks_locked();
            healthy=boot_config_ok&&config_storage_ok&&web_started&&wallbox_ready&&meter_ready&&house_bus_ready&&contact_gpio_ready&&
                (!settings.phase_switch_enabled||phase_gpio_ready)&&runtime_stale_tasks(boot_task_seen,required,now)==0;
            UNLOCK();
        }
        if(validated){
            uint32_t failed=runtime_check_step(&runtime,now,runtime_stale_tasks(boot_task_seen,required,now));
            if(failed){
                bool first=!runtime_watchdog_failed;
                runtime_watchdog_tasks|=failed;runtime_watchdog_failed=true;
                if(xSemaphoreTake(state_lock,ticks(100))==pdTRUE){
                    settings.enabled=false;
                    if(!runtime_fault_recorded){event_locked("runtime_watchdog");runtime_fault_recorded=true;}
                    UNLOCK();
                }
                if(first)ESP_LOGE(TAG,"Runtime task supervision stopped charging (mask 0x%lx)",(unsigned long)failed);
            }
            vTaskDelay(ticks(500));continue;
        }
        int result=boot_check_step(&check,now,healthy);
        if(result==BOOT_CHECK_PASS){
            esp_err_t err=boot_rollback_armed?esp_ota_mark_app_valid_cancel_rollback():ESP_OK;
            if(err==ESP_OK){LOCK();
                if(phase_startup){phase_stop_at=phase_move_at=now_ms();phase_zero_at=0;}
                boot_validation_pending=false;boot_validation_ok=true;event_locked("firmware_verified");UNLOCK();
                validated=true;ESP_LOGI(TAG,"Firmware startup check passed; runtime task supervision active");
#ifdef TEE_BOOT_CHECK_TEST
                vTaskDelete(NULL);return;
#else
                continue;
#endif
            }
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
