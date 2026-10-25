#include "json_owned.h"
static esp_err_t huawei_scan_get(httpd_req_t *req){
    cJSON *o=cJSON_CreateObject();if(!o)return send_json(req,NULL);
    cJSON *list=cJSON_AddArrayToObject(o,"devices");LOCK();
    cJSON_AddBoolToObject(o,"running",huawei_scan_running);cJSON_AddBoolToObject(o,"partial",huawei_scan_partial);
    cJSON_AddNumberToObject(o,"progress",huawei_scan_progress);
    for(unsigned i=0;i<huawei_found_count;i++){
        cJSON *d=cJSON_CreateObject();cJSON_AddStringToObject(d,"host",huawei_found[i].host);
        cJSON_AddStringToObject(d,"model",huawei_found[i].model);cJSON_AddNumberToObject(d,"unit_id",huawei_found[i].unit);
        json_add_owned_to_array(list,d);
    }
    UNLOCK();return send_json(req,o);
}
static void huawei_scan_task(void *arg){
    (void)arg;bool complete=false;esp_netif_ip_info_t info;
    esp_netif_t *station=esp_netif_get_handle_from_ifkey("WIFI_STA_DEF");
    if(station&&esp_netif_get_ip_info(station,&info)==ESP_OK&&info.ip.addr){
        uint32_t local=ntohl(info.ip.addr),mask=ntohl(info.netmask.addr);if(mask<0xffffff00U)mask=0xffffff00U;
        uint32_t first=(local&mask)+1,last=(local&mask)|~mask;int64_t deadline=now_ms()+150000;
        for(uint32_t ip=first;ip<last;ip++){
            LOCK();bool stop=!wifi_online||restarting||ota_in_progress||now_ms()>deadline;uint8_t configured=settings.huawei_unit_id;UNLOCK();
            if(stop)break;
            if(ip!=local&&meter_port_open(ip,502)){
                char host[64],model[31];snprintf(host,sizeof(host),"%u.%u.%u.%u",(unsigned)(ip>>24),(unsigned)((ip>>16)&255),(unsigned)((ip>>8)&255),(unsigned)(ip&255));
                uint8_t units[3]={configured,1,0};
                for(unsigned n=0;n<3;n++){
                    bool duplicate=false;for(unsigned j=0;j<n;j++)if(units[j]==units[n])duplicate=true;if(duplicate)continue;
                    if(huawei_probe(host,units[n],model)){
                        LOCK();if(huawei_found_count<8){huawei_found_t *d=&huawei_found[huawei_found_count++];strcpy(d->host,host);strcpy(d->model,model);d->unit=units[n];}UNLOCK();break;
                    }
                    if(now_ms()>deadline)break;
                }
            }
            LOCK();huawei_scan_progress=(ip-first+1)*100/(last-first);UNLOCK();
            if(ip+1==last)complete=true;
            vTaskDelay(ticks(25));
        }
    }
    LOCK();huawei_scan_partial=!complete;huawei_scan_running=false;UNLOCK();vTaskDelete(NULL);
}
static esp_err_t huawei_scan_post(httpd_req_t *req){
    if(!authorized(req))return ESP_OK;
    LOCK();bool available=wifi_online&&!wifi_ap_forced&&!restarting&&!ota_in_progress;
    bool busy=huawei_scan_running||huawei_preview_running||shelly_scan_running||meter_preview.running||
        (settings.enabled&&settings.huawei_enabled);
    bool recent=huawei_scan_started&&now_ms()-huawei_scan_started<60000;
    if(available&&!busy&&!recent){huawei_scan_running=true;huawei_scan_partial=false;huawei_found_count=huawei_scan_progress=0;huawei_scan_started=now_ms();}
    UNLOCK();
    if(!available||busy||recent)return httpd_resp_send_err(req,HTTPD_400_BAD_REQUEST,"Heimnetz verbinden, Suche abwarten oder Ladung fuer die Huawei-Suche stoppen.");
    if(xTaskCreate(huawei_scan_task,"huawei_scan",4096,NULL,3,NULL)!=pdPASS){LOCK();huawei_scan_running=false;UNLOCK();return httpd_resp_send_err(req,HTTPD_500_INTERNAL_SERVER_ERROR,"Suche konnte nicht gestartet werden.");}
    return huawei_scan_get(req);
}
static esp_err_t huawei_preview_get(httpd_req_t *req){
    cJSON *o=cJSON_CreateObject();if(!o)return send_json(req,NULL);LOCK();huawei_reading_t r=huawei_preview;bool running=huawei_preview_running;UNLOCK();
    cJSON_AddBoolToObject(o,"running",running);cJSON_AddStringToObject(o,"model",r.model);cJSON_AddStringToObject(o,"host",r.host);cJSON_AddNumberToObject(o,"unit_id",r.unit);
    cJSON_AddBoolToObject(o,"connected",r.connected);nullable(o,"house_power_w",r.grid_w,r.grid_ok);
    cJSON_AddBoolToObject(o,"house_current_ok",r.current_ok);
    json_add_owned_to_object(o,"house_a",number_array(r.grid_a,3,r.current_ok));
    nullable(o,"pv_generation_w",r.pv_w,r.pv_ok);nullable(o,"battery_soc_pct",r.soc,r.battery_ok);
    nullable(o,"battery_charge_w",r.charge_w,r.battery_ok);nullable(o,"battery_discharge_w",r.discharge_w,r.battery_ok);
    return send_json(req,o);
}
static void huawei_preview_task(void *arg){
    (void)arg;char host[64];uint8_t unit;LOCK();strcpy(host,huawei_preview.host);unit=huawei_preview.unit;UNLOCK();
    huawei_reading_t r;huawei_read(host,unit,true,true,true,&r);
    LOCK();huawei_preview=r;huawei_preview_running=false;UNLOCK();vTaskDelete(NULL);
}
static esp_err_t huawei_preview_post(httpd_req_t *req){
    if(!authorized(req))return ESP_OK;
    cJSON *o=read_json(req);if(!o)return ESP_OK;
    cJSON *host=cJSON_GetObjectItem(o,"host"),*unit=cJSON_GetObjectItem(o,"unit_id");
    bool valid=cJSON_IsString(host)&&host->valuestring[0]&&strlen(host->valuestring)<64&&cJSON_IsNumber(unit)&&
        isfinite(unit->valuedouble)&&floor(unit->valuedouble)==unit->valuedouble&&unit->valuedouble>=0&&unit->valuedouble<=247;
    if(valid)for(const unsigned char *p=(const unsigned char *)host->valuestring;*p;p++)if(!isalnum(*p)&&*p!='.'&&*p!='-'&&*p!=':')valid=false;
    LOCK();bool ready=wifi_online&&!restarting&&!ota_in_progress&&!huawei_preview_running&&!huawei_scan_running&&!shelly_scan_running&&!meter_preview.running&&!(settings.enabled&&settings.huawei_enabled);
    if(valid&&ready){memset(&huawei_preview,0,sizeof(huawei_preview));strcpy(huawei_preview.host,host->valuestring);huawei_preview.unit=(uint8_t)unit->valuedouble;huawei_preview_running=true;}
    UNLOCK();cJSON_Delete(o);
    if(!valid||!ready)return httpd_resp_send_err(req,HTTPD_400_BAD_REQUEST,"Huawei-Adresse pruefen oder laufende Suche abwarten.");
    if(xTaskCreate(huawei_preview_task,"huawei_preview",4096,NULL,3,NULL)!=pdPASS){LOCK();huawei_preview_running=false;UNLOCK();return httpd_resp_send_err(req,HTTPD_500_INTERNAL_SERVER_ERROR,"Pruefung konnte nicht gestartet werden.");}
    return huawei_preview_get(req);
}
