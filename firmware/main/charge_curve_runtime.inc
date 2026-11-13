/* Optional graph storage must never affect charging or energy accounting. */
static charge_curve_t charge_curve;
static uint32_t charge_curve_saved_revision;
static int64_t charge_curve_save_at,charge_curve_save_attempt;
static void load_charge_curve(nvs_handle_t nvs) {
    size_t size=0;
    if(nvs_get_blob(nvs,"charge_curve",NULL,&size)!=ESP_OK||size!=sizeof(charge_curve_t))return;
    charge_curve_t *copy=malloc(sizeof(*copy));if(!copy)return;
    if(nvs_get_blob(nvs,"charge_curve",copy,&size)==ESP_OK)charge_curve_restore(&charge_curve,copy);
    charge_curve_saved_revision=charge_curve.revision;free(copy);
}
static void save_charge_curve(int64_t now,bool force) {
    LOCK();bool due=charge_curve.count&&charge_curve.revision!=charge_curve_saved_revision&&
        (force||!charge_curve.active||now-charge_curve_save_at>=900000)&&(force||now-charge_curve_save_attempt>=60000);UNLOCK();
    if(!stats_storage_ok||!due)return;
    charge_curve_t *copy=malloc(sizeof(*copy));if(!copy)return;
    LOCK();*copy=charge_curve;charge_curve_save_attempt=now;UNLOCK();
    nvs_handle_t nvs;esp_err_t err=nvs_open_from_partition("stats","energy",NVS_READWRITE,&nvs);
    if(err==ESP_OK){err=nvs_set_blob(nvs,"charge_curve",copy,sizeof(*copy));if(err==ESP_OK)err=nvs_commit(nvs);nvs_close(nvs);}
    if(err==ESP_OK){LOCK();charge_curve_saved_revision=copy->revision;charge_curve_save_at=now;UNLOCK();}
    else ESP_LOGW(TAG,"Optional charge curve checkpoint failed: %s",esp_err_to_name(err));
    free(copy);
}
static esp_err_t charge_curve_handler(httpd_req_t *req) {
    charge_curve_t *copy=malloc(sizeof(*copy));
    if(!copy){httpd_resp_set_status(req,"503 Service Unavailable");return httpd_resp_send(req,"Verlauf derzeit nicht verfuegbar.",HTTPD_RESP_USE_STRLEN);}
    LOCK();*copy=charge_curve;UNLOCK();
    httpd_resp_set_type(req,"application/json");httpd_resp_set_hdr(req,"Cache-Control","no-store");
    char chunk[1024];int used=snprintf(chunk,sizeof(chunk),"{\"active\":%s,\"interrupted\":%s,\"start_epoch\":%lu,\"end_epoch\":%lu,\"duration_s\":%lu,\"interval_s\":%lu,\"points\":[",
        copy->active?"true":"false",copy->interrupted?"true":"false",(unsigned long)copy->start_epoch,(unsigned long)copy->end_epoch,(unsigned long)copy->duration_s,(unsigned long)copy->interval_s);
    esp_err_t err=ESP_OK;
    bool live=copy->count&&copy->live.s>copy->points[copy->count-1].s;
    for(uint32_t i=0;i<copy->count+(live?1:0)&&err==ESP_OK;i++) {
        const charge_curve_point_t *p=i<copy->count?&copy->points[i]:&copy->live;
        char w[24],goal[24],low[24],high[24];
        if(isfinite(p->w))snprintf(w,sizeof(w),"%.1f",p->w);else strcpy(w,"null");
        if(isfinite(p->low_w))snprintf(low,sizeof(low),"%.1f",p->low_w);else strcpy(low,"null");
        if(isfinite(p->high_w))snprintf(high,sizeof(high),"%.1f",p->high_w);else strcpy(high,"null");
        if(isfinite(p->target_w))snprintf(goal,sizeof(goal),"%.1f",p->target_w);else strcpy(goal,"null");
        used+=snprintf(chunk+used,sizeof(chunk)-(size_t)used,"%s{\"s\":%lu,\"w\":%s,\"target_w\":%s,\"low_w\":%s,\"high_w\":%s}",i?",":"",(unsigned long)p->s,w,goal,low,high);
        if(used>750){err=httpd_resp_send_chunk(req,chunk,used);used=0;}
    }
    if(err==ESP_OK){used+=snprintf(chunk+used,sizeof(chunk)-(size_t)used,"]}");err=httpd_resp_send_chunk(req,chunk,used);}
    free(copy);if(err!=ESP_OK)return err;
    return httpd_resp_send_chunk(req,NULL,0);
}
