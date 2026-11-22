/* Keep one consistent copy while the browser reads. No JSON tree per minute. */
typedef struct {
    double watts[EMS_HISTORY][EMS_SOURCES];
    energy_day_reader_t days;
} history_snapshot_t;

static void history_number(char out[32],double value,bool valid){
    if(valid&&isfinite(value))snprintf(out,32,"%.15g",value);
    else strcpy(out,"null");
}

static esp_err_t history_handler(httpd_req_t *req){
    history_snapshot_t *copy=malloc(sizeof(*copy));
    if(!copy)return httpd_resp_send_err(req,HTTPD_500_INTERNAL_SERVER_ERROR,"Out of memory");
    int64_t epoch,midnight;
    if(energy_day_reader_open(&copy->days,calendar(&epoch,&midnight))!=ESP_OK){
        free(copy);
        return httpd_resp_send_err(req,HTTPD_500_INTERNAL_SERVER_ERROR,"Tageswerte nicht lesbar.");
    }
    LOCK();
    int64_t last=now_ms()/60000;
    int64_t start=last>=EMS_HISTORY?last-EMS_HISTORY+1:0;
    for(int64_t m=start;m<=last;m++){
        const energy_point_t *p=&energy.history[m%EMS_HISTORY];
        for(unsigned s=0;s<EMS_SOURCES;s++){
            uint32_t ms=p->covered_ms[s];
            copy->watts[m-start][s]=p->minute==m&&ms?p->wh[s]*3600000/ms:NAN;
        }
    }
    UNLOCK();
    httpd_resp_set_type(req,"application/json; charset=utf-8");
    httpd_resp_set_hdr(req,"Cache-Control","no-store");
    httpd_resp_set_hdr(req,"X-Content-Type-Options","nosniff");
    char chunk[1024]="{\"points\":[";
    size_t used=strlen(chunk);
    bool first=true;
    esp_err_t err=ESP_OK;
    for(int64_t m=start;m<=last&&err==ESP_OK;m++){
        char values[EMS_SOURCES][32];
        for(unsigned s=0;s<EMS_SOURCES;s++)history_number(values[s],copy->watts[m-start][s],true);
        used+=snprintf(chunk+used,sizeof(chunk)-used,"%s{\"age_min\":%u,\"estimate_w\":%s,\"wallbox_w\":%s}",
                       first?"":",",(unsigned)(last-m),values[0],values[1]);
        first=false;
        if(used>700){err=httpd_resp_send_chunk(req,chunk,used);used=0;}
    }
    used+=snprintf(chunk+used,sizeof(chunk)-used,"],\"days\":[");
    first=true;
    energy_day_t record;
    while(err==ESP_OK){
        bool found=false;
        err=energy_day_reader_next(&copy->days,&record,&found);
        if(err!=ESP_OK||!found)break;
        const energy_day_t *day=&record;
        char values[EMS_SOURCES][32];
        for(unsigned s=0;s<EMS_SOURCES;s++)history_number(values[s],day->wh[s]/1000,day->covered_ms[s]>0);
        used+=snprintf(chunk+used,sizeof(chunk)-used,"%s{\"date\":%lu,\"estimate_kwh\":%s,\"estimate_covered_s\":%lu,"
                       "\"wallbox_kwh\":%s,\"wallbox_covered_s\":%lu}",first?"":",",(unsigned long)day->date,
                       values[0],(unsigned long)(day->covered_ms[0]/1000),values[1],(unsigned long)(day->covered_ms[1]/1000));
        first=false;
        if(used>700){err=httpd_resp_send_chunk(req,chunk,used);used=0;}
    }
    energy_day_reader_close(&copy->days);
    free(copy);
    if(err!=ESP_OK)return err;
    chunk[used++]=']';chunk[used++]='}';
    err=httpd_resp_send_chunk(req,chunk,used);
    return err==ESP_OK?httpd_resp_send_chunk(req,NULL,0):err;
}
