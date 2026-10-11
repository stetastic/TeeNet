/* Raw flash contains the bootloader, firmware slots, configuration and stats.
   Hold persistence while streaming; never buffer the image in ESP RAM. */
static esp_err_t system_backup_handler(httpd_req_t *req){
    if(!authorized(req))return ESP_OK;
    LOCK();int64_t now=now_ms();bool allowed=!settings.enabled&&!restarting&&!ota_in_progress&&!github_update.running&&
        !(fresh(now,actual_at,EMS_METER_TTL)&&(actual_a[0]>=1||actual_a[1]>=1||actual_a[2]>=1));
    if(allowed){backup_in_progress=true;ota_in_progress=true;}UNLOCK();
    if(!allowed){httpd_resp_set_status(req,"409 Conflict");return httpd_resp_sendstr(req,"Vor der Systemsicherung Ladung stoppen und Wartung abwarten.");}
    esp_err_t result=ESP_FAIL;uint32_t size=0;uint8_t *buffer=NULL;
    bool locked=xSemaphoreTake(maintenance_flash_lock,ticks(2000))==pdTRUE;
    if(!locked)goto done;
    reset_sequence_clear();
    if(!save_energy()||!save_plan()||!save_control_memory(true))goto done;
    save_diagnostics(true);
    if(esp_flash_get_size(NULL,&size)!=ESP_OK||size>32*1024*1024||size<4*1024*1024)goto done;
    buffer=malloc(4096);if(!buffer)goto done;
    char filename[128],length[24];snprintf(filename,sizeof(filename),"attachment; filename=\"TeeNet-%s-fullflash.bin\"",EMS_VERSION);
    snprintf(length,sizeof(length),"%" PRIu32,size);
    httpd_resp_set_type(req,"application/octet-stream");httpd_resp_set_hdr(req,"Content-Disposition",filename);
    httpd_resp_set_hdr(req,"Cache-Control","no-store");httpd_resp_set_hdr(req,"X-TeeNet-Backup-Bytes",length);
    result=ESP_OK;
    for(uint32_t at=0;at<size;at+=4096){
        uint32_t count=size-at>4096?4096:size-at;
        if(esp_flash_read(NULL,buffer,at,count)!=ESP_OK||httpd_resp_send_chunk(req,(char *)buffer,count)!=ESP_OK){result=ESP_FAIL;break;}
        vTaskDelay(1);
    }
    if(result==ESP_OK)result=httpd_resp_send_chunk(req,NULL,0);
done:
    free(buffer);if(locked)xSemaphoreGive(maintenance_flash_lock);
    LOCK();backup_in_progress=false;ota_in_progress=false;UNLOCK();
    /* An interrupted chunked response must not look like a complete backup. */
    return result;
}
