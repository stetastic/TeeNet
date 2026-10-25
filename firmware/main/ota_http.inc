/* Verify application identity before touching the inactive OTA partition. */
static esp_err_t ota_handler(httpd_req_t *req) {
    if(!authorized(req))return ESP_OK;
    const esp_partition_t *partition=esp_ota_get_next_update_partition(NULL);
    if(!partition||req->content_len<1024||req->content_len>partition->size)
        return httpd_resp_send_err(req,HTTPD_400_BAD_REQUEST,"Passende TeeNet-OTA-Datei bis 2 MB auswaehlen.");
    uint8_t prefix[EMS_FIRMWARE_PREFIX];size_t received=0;int timeouts=0;
    int64_t prefix_deadline=now_ms()+4000;
    while(received<EMS_FIRMWARE_PREFIX&&now_ms()<prefix_deadline) {
        int n=httpd_req_recv(req,(char *)prefix+received,EMS_FIRMWARE_PREFIX-received);
        if(n==HTTPD_SOCK_ERR_TIMEOUT&&++timeouts<=2)continue;
        if(n<=0)return httpd_resp_send_err(req,HTTPD_400_BAD_REQUEST,"Firmware-Kopf unvollstaendig.");
        received+=n;
    }
    if(received!=EMS_FIRMWARE_PREFIX||now_ms()>prefix_deadline)
        return httpd_resp_send_err(req,HTTPD_408_REQ_TIMEOUT,"Firmware-Kopf zu langsam. Erneut versuchen.");
    char version[32];
    if(!firmware_prefix_check(prefix,sizeof(prefix),version))
        return httpd_resp_send_err(req,HTTPD_400_BAD_REQUEST,"Datei ist keine TeeNet-Anwendung fuer ESP32-S3. Keine USB-/Factory-Datei verwenden.");
    LOCK();int64_t now=now_ms();
    bool allowed=!settings.enabled&&!restarting&&!ota_in_progress&&
        !(fresh(now,actual_at,EMS_METER_TTL)&&(actual_a[0]>=1||actual_a[1]>=1||actual_a[2]>=1));
    if(allowed){ota_in_progress=true;settings.enabled=false;}UNLOCK();
    if(!allowed){httpd_resp_set_status(req,"409 Conflict");return httpd_resp_sendstr(req,"Zuerst Ladung stoppen und Ende der Ladung abwarten.");}
    esp_ota_handle_t handle=0;
    bool opened=esp_ota_begin(partition,req->content_len,&handle)==ESP_OK;
    uint8_t *buffer=opened?malloc(4096):NULL;bool ok=buffer&&esp_ota_write(handle,prefix,sizeof(prefix))==ESP_OK;
    int64_t deadline=now_ms()+180000;timeouts=0;
    while(ok&&received<req->content_len&&now_ms()<deadline) {
        int n=httpd_req_recv(req,(char *)buffer,req->content_len-received>4096?4096:req->content_len-received);
        if(n==HTTPD_SOCK_ERR_TIMEOUT&&++timeouts<=2)continue;
        if(n<=0||esp_ota_write(handle,buffer,n)!=ESP_OK)ok=false;else {received+=n;timeouts=0;}
    }
    free(buffer);ok=ok&&received==req->content_len;
    if(!ok){if(opened)esp_ota_abort(handle);}
    else {ok=esp_ota_end(handle)==ESP_OK;if(ok)ok=esp_ota_set_boot_partition(partition)==ESP_OK;}
    LOCK();ota_in_progress=false;if(ok)restarting=true;UNLOCK();
    if(!ok)return httpd_resp_send_err(req,HTTPD_500_INTERNAL_SERVER_ERROR,"Update abgebrochen oder Datei beschaedigt. Bisherige Firmware bleibt aktiv; Ladung bleibt ausgeschaltet.");
    cJSON *result=cJSON_CreateObject();if(!result)return send_json(req,NULL);
    cJSON_AddBoolToObject(result,"ok",true);cJSON_AddStringToObject(result,"version",version);
    return send_json(req,result);
}
