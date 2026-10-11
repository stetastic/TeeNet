/* Preferences survive reboots; charging commands never do. */
static void load_control_memory(void) {
    nvs_handle_t handle;size_t size=sizeof(stored_control_memory);
    if(!config_storage_ok||nvs_open("wallbox_ems",NVS_READONLY,&handle)!=ESP_OK)return;
    esp_err_t err=nvs_get_blob(handle,"control_memory",&stored_control_memory,&size);nvs_close(handle);
    if(err==ESP_ERR_NVS_NOT_FOUND)return;
    if(err!=ESP_OK||!control_memory_valid(&stored_control_memory,size)){
        memset(&stored_control_memory,0,sizeof(stored_control_memory));
        control_storage_ok=false;return;
    }
    if(settings.battery_protect&&settings.zero_feed_enabled&&!settings.basic_mode){battery_use=(stored_control_memory.flags&1)!=0;battery_start_use=(stored_control_memory.flags&2)!=0;}
    control_memory_pending=true;
}

/* Caller holds state_lock. Wait for the clock before translating UTC deadlines.
   An explicit control command or backup may resolve conservatively without it. */
static void restore_control_memory_locked(int64_t now,int64_t epoch,bool force) {
    if(!control_memory_pending || (!force && epoch<=0 && now<20000))return;
    control_memory_restore(&stored_control_memory,&battery_buffer,&pv_control,now,epoch);
    if(!battery_use){battery_buffer.started=false;battery_buffer.remaining_ms=0;}
    control_memory_pending=false;
}

/* Caller holds maintenance_flash_lock, never state_lock. Write on preference
   or deadline changes rather than every countdown tick. */
static bool save_control_memory(bool force) {
    int64_t epoch,midnight,now=now_ms();uint32_t date=calendar(&epoch,&midnight);control_memory_t snapshot;
    LOCK();restore_control_memory_locked(now,date?epoch:0,force);
    if(control_memory_pending){UNLOCK();return true;}
    control_memory_capture(&snapshot,&battery_buffer,&pv_control,battery_use,battery_start_use,now,date?epoch:0);
    bool changed=force||!control_storage_ok||!stored_control_memory.version||snapshot.flags!=stored_control_memory.flags||
        llabs(snapshot.cloud_epoch_ms-stored_control_memory.cloud_epoch_ms)>2000||llabs(snapshot.cooldown_epoch_ms-stored_control_memory.cooldown_epoch_ms)>2000||
        (!snapshot.cloud_ms!=!stored_control_memory.cloud_ms)||(!snapshot.cooldown_ms!=!stored_control_memory.cooldown_ms);
    UNLOCK();if(!changed)return true;
    nvs_handle_t handle;esp_err_t err=config_storage_ok?nvs_open("wallbox_ems",NVS_READWRITE,&handle):ESP_FAIL;
    if(err==ESP_OK){err=nvs_set_blob(handle,"control_memory",&snapshot,sizeof(snapshot));if(err==ESP_OK)err=nvs_commit(handle);nvs_close(handle);}
    LOCK();control_storage_ok=err==ESP_OK;if(control_storage_ok)stored_control_memory=snapshot;UNLOCK();return err==ESP_OK;
}
