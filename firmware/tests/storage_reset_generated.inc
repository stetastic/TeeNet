/* Generated from main/app.c; do not edit. */
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

static esp_err_t energy_reset_handler(httpd_req_t *req) {
    if(!authorized(req)) return ESP_OK;
    int64_t epoch,minute;uint32_t date=calendar(&epoch,&minute);
    if(!date){httpd_resp_set_status(req,"503 Service Unavailable");return httpd_resp_sendstr(req,"Internetzeit noch nicht verfuegbar.");}
    if(xSemaphoreTake(maintenance_flash_lock,ticks(2000))!=pdTRUE){httpd_resp_set_status(req,"409 Conflict");return httpd_resp_sendstr(req,"Speicherung beschaeftigt. Zaehlerstand noch nicht zurueckgesetzt.");}
    double previous[EMS_SOURCES];uint32_t previous_date;
    LOCK();memcpy(previous,energy_reset_wh,sizeof(previous));previous_date=energy_reset_date;
    for(int i=0;i<EMS_SOURCES;i++)energy_reset_wh[i]=energy.store.total_wh[i];
    energy_reset_date=date;UNLOCK();
    if(!save_energy()){
        LOCK();memcpy(energy_reset_wh,previous,sizeof(previous));energy_reset_date=previous_date;UNLOCK();
        xSemaphoreGive(maintenance_flash_lock);
        return httpd_resp_send_err(req,HTTPD_500_INTERNAL_SERVER_ERROR,"Zaehlerstand konnte nicht gespeichert werden.");
    }
    xSemaphoreGive(maintenance_flash_lock);
    return ok_json(req);
}

static esp_err_t sessions_reset_handler(httpd_req_t *req) {
    if(!authorized(req)) return ESP_OK;
    sessions_t *previous=malloc(sizeof(*previous));
    if(!previous)return httpd_resp_send_err(req,HTTPD_500_INTERNAL_SERVER_ERROR,"Nicht genug Speicher.");
    if(xSemaphoreTake(maintenance_flash_lock,ticks(2000))!=pdTRUE){free(previous);httpd_resp_set_status(req,"409 Conflict");return httpd_resp_sendstr(req,"Speicherung beschaeftigt. Ladesitzungen noch nicht geloescht.");}
    LOCK();*previous=sessions;sessions_init(&sessions);UNLOCK();
    if(!save_energy()){
        LOCK();sessions=*previous;UNLOCK();free(previous);
        xSemaphoreGive(maintenance_flash_lock);
        return httpd_resp_send_err(req,HTTPD_500_INTERNAL_SERVER_ERROR,"Ladesitzungen konnten nicht geloescht werden.");
    }
    xSemaphoreGive(maintenance_flash_lock);free(previous);return ok_json(req);
}

