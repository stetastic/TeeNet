#include "json_owned.h"
/* Shared, bounded diagnostic response. No credentials or network scanning. */
static cJSON *event_json(const ems_event_t *e) {
    cJSON *row=cJSON_CreateObject();if(!row)return NULL;
    cJSON_AddNumberToObject(row,"sequence",e->sequence);cJSON_AddNumberToObject(row,"epoch",e->epoch_s);
    cJSON_AddNumberToObject(row,"uptime_s",e->uptime_s);cJSON_AddStringToObject(row,"reason",e->reason);
    nullable(row,"target_kw",e->target_kw,isfinite(e->target_kw));nullable(row,"actual_kw",e->actual_kw,isfinite(e->actual_kw));
    nullable(row,"house_kw",e->house_kw,isfinite(e->house_kw));nullable(row,"battery_pct",e->battery_pct,isfinite(e->battery_pct));return row;
}
static esp_err_t events_handler(httpd_req_t *req) {
    ems_events_t *copy=malloc(sizeof(*copy));diagnostic_store_t *faults=malloc(sizeof(*faults));
    if(!copy||!faults){free(copy);free(faults);return send_json(req,NULL);}
    LOCK();*copy=event_log;*faults=fault_log;bool saved=fault_storage_ok;UNLOCK();
    cJSON *o=cJSON_CreateObject();if(!o){free(copy);free(faults);return send_json(req,NULL);}
    cJSON *rows=cJSON_AddArrayToObject(o,"events"),*retained=cJSON_AddArrayToObject(o,"persisted_faults");
    if(!rows||!retained){free(copy);free(faults);cJSON_Delete(o);return send_json(req,NULL);}
    for(unsigned i=0;i<copy->count;i++)json_add_owned_to_array(rows,event_json(ems_event_recent(copy,i)));
    for(unsigned i=0;i<faults->count;i++)json_add_owned_to_array(retained,event_json(diagnostic_store_recent(faults,i)));
    cJSON_AddBoolToObject(o,"storage_ok",saved);free(copy);free(faults);return send_json(req,o);
}
