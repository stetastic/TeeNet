#include "json_owned.h"
/* Shared, bounded diagnostic response. No credentials or network scanning. */
static cJSON *event_json(const ems_event_t *e) {
    cJSON *row=cJSON_CreateObject();if(!row)return NULL;
    cJSON_AddNumberToObject(row,"sequence",e->sequence);cJSON_AddNumberToObject(row,"epoch",e->epoch_s);
    cJSON_AddNumberToObject(row,"uptime_s",e->uptime_s);cJSON_AddStringToObject(row,"reason",e->reason);
    nullable(row,"target_kw",e->target_kw,isfinite(e->target_kw));nullable(row,"actual_kw",e->actual_kw,isfinite(e->actual_kw));
    nullable(row,"house_kw",e->house_kw,isfinite(e->house_kw));nullable(row,"battery_pct",e->battery_pct,isfinite(e->battery_pct));
    if(cJSON_GetArraySize(row)!=8){cJSON_Delete(row);return NULL;}return row;
}
/* Coalesce diagnostic rows into bounded chunks. With TCP_NODELAY, sending
   each comma/row separately creates many tiny TCP packets on small boards. */
typedef struct {httpd_req_t *req;size_t used;char data[1024];} event_output_t;
static esp_err_t event_output_flush(event_output_t *out){
    if(!out->used)return ESP_OK;
    esp_err_t err=httpd_resp_send_chunk(out->req,out->data,out->used);
    if(err==ESP_OK)out->used=0;
    return err;
}
static esp_err_t event_output_append(event_output_t *out,const char *text,size_t length){
    while(length){
        size_t count=sizeof(out->data)-out->used;if(count>length)count=length;
        memcpy(out->data+out->used,text,count);out->used+=count;text+=count;length-=count;
        if(out->used==sizeof(out->data)){
            esp_err_t err=event_output_flush(out);if(err!=ESP_OK)return err;
        }
    }
    return ESP_OK;
}
static esp_err_t event_send_row(event_output_t *out,const ems_event_t *event,bool comma){
    cJSON *row=event_json(event);char *body=row?json_print_compact(row):NULL;cJSON_Delete(row);
    if(!body)return ESP_FAIL;
    esp_err_t err=comma?event_output_append(out,",",1):ESP_OK;
    if(err==ESP_OK)err=event_output_append(out,body,strlen(body));
    free(body);return err;
}
static esp_err_t events_handler(httpd_req_t *req) {
    if(xSemaphoreTake(status_snapshot_lock,ticks(1000))!=pdTRUE){httpd_resp_set_status(req,"503 Service Unavailable");return httpd_resp_sendstr(req,"Diagnose kurz beschaeftigt. Erneut versuchen.");}
    ems_events_t *copy=malloc(sizeof(*copy));diagnostic_store_t *faults=malloc(sizeof(*faults));
    if(!copy||!faults){free(copy);free(faults);xSemaphoreGive(status_snapshot_lock);httpd_resp_set_status(req,"503 Service Unavailable");return httpd_resp_sendstr(req,"Diagnose kurz beschaeftigt. Erneut versuchen.");}
    LOCK();*copy=event_log;*faults=fault_log;bool saved=fault_storage_ok;UNLOCK();
    /* A full log must not create hundreds of retained cJSON nodes at once. */
    httpd_resp_set_type(req,"application/json; charset=utf-8");httpd_resp_set_hdr(req,"Cache-Control","no-store");
    event_output_t out={.req=req};
    esp_err_t err=event_output_append(&out,"{\"events\":[",strlen("{\"events\":["));
    for(unsigned i=0;i<copy->count&&err==ESP_OK;i++)err=event_send_row(&out,ems_event_recent(copy,i),i!=0);
    if(err==ESP_OK)err=event_output_append(&out,"],\"persisted_faults\":[",strlen("],\"persisted_faults\":["));
    for(unsigned i=0;i<faults->count&&err==ESP_OK;i++)err=event_send_row(&out,diagnostic_store_recent(faults,i),i!=0);
    const char *tail=saved?"],\"storage_ok\":true}":"],\"storage_ok\":false}";
    if(err==ESP_OK)err=event_output_append(&out,tail,strlen(tail));
    free(copy);free(faults);
    if(err==ESP_OK)err=event_output_flush(&out);
    if(err==ESP_OK)err=httpd_resp_send_chunk(req,NULL,0);
    xSemaphoreGive(status_snapshot_lock);return err;
}
