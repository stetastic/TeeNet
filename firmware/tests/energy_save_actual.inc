static bool save_energy(void) {

    if(!stats_storage_ok) return false;
    /* Never compete with a status tree, and never stall the control loop while
       a slow HTTP client retains one. A skipped checkpoint is retried soon. */
    if(xSemaphoreTake(status_snapshot_lock,0)!=pdTRUE)return false;

    meter_totals_t *snapshot=malloc(sizeof(*snapshot));
    session_checkpoint_t *session_snapshot=NULL;
    double reset_snapshot[EMS_SOURCES]={0};uint32_t reset_date_snapshot=0;
    bool captured=false;
    for(unsigned attempt=0;snapshot&&attempt<3&&!captured;attempt++){
        LOCK();unsigned count=sessions.store.count;UNLOCK();
        if(count>CHARGE_SESSION_CAPACITY)break;
        session_snapshot=malloc(sizeof(*session_snapshot)+count*sizeof(session_snapshot->records[0]));
        if(!session_snapshot)break;
        LOCK();captured=session_checkpoint_copy(session_snapshot,count,&sessions.store);
        if(captured){*snapshot=(meter_totals_t){.version=1,.totals=energy.store,.counter=charge_counter.saved};if(!snapshot->counter.version)snapshot->counter.version=1;memcpy(reset_snapshot,energy_reset_wh,sizeof(reset_snapshot));reset_date_snapshot=energy_reset_date;}
        UNLOCK();
        if(!captured){free(session_snapshot);session_snapshot=NULL;}
    }
    if(!captured){free(snapshot);free(session_snapshot);LOCK();storage_error=true;UNLOCK();xSemaphoreGive(status_snapshot_lock);ESP_LOGW(TAG,"Energy snapshot allocation failed");return false;}

    nvs_handle_t nvs; esp_err_t err=nvs_open_from_partition("stats","energy",NVS_READWRITE,&nvs);

    if(err==ESP_OK) {

        err=nvs_set_blob(nvs,"meter_totals",snapshot,sizeof(*snapshot));
        if(err==ESP_OK)err=nvs_set_blob(nvs,"totals",&snapshot->totals,sizeof(snapshot->totals));
        if(err==ESP_OK)err=session_chunks_save_checkpoint(nvs,session_snapshot);
        if(err==ESP_OK)err=nvs_set_blob(nvs,"reset_base",reset_snapshot,sizeof(reset_snapshot));
        if(err==ESP_OK)err=nvs_set_u32(nvs,"reset_date",reset_date_snapshot);

        if(err==ESP_OK) err=nvs_commit(nvs);

        nvs_close(nvs);

    }

    free(snapshot);free(session_snapshot); LOCK(); storage_error=err!=ESP_OK; if(err==ESP_OK){last_saved_ms=now_ms();sessions.checkpoint=false;} UNLOCK();
    xSemaphoreGive(status_snapshot_lock);
    if(err!=ESP_OK)ESP_LOGW(TAG,"Energy save failed: %s",esp_err_to_name(err));

    return err==ESP_OK;

}

