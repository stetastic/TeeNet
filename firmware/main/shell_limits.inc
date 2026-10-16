/* One background worker: Shell configuration and optional vehicle status.
   chargingRate is never used as current or power feedback. */
#include "shell_search_schedule.h"
static int shell_read_snapshot(const char *host,float *grid,float *maximum,evcc_shell_t *sample) {
    char url[112];int n=snprintf(url,sizeof(url),"http://%s:12800/user/status",host);
    if(n<=0 || (size_t)n>=sizeof(url))return 0;
    char *body=malloc(6144);if(!body)return 0;
    int code=http_get_body_mode(url,body,6144,5000,true);
    if(code==200) {
        if(!shell_limits_parse(body,grid,maximum))*grid=*maximum=0;
        if(sample && !evcc_shell_parse(body,sample))strcpy(sample->reason,"invalid_json");
    }
    free(body);return code;
}
static int shell_read_limits(const char *host,float *grid,float *maximum) {
    int code=shell_read_snapshot(host,grid,maximum,NULL);
    return code==200 && *maximum==0?422:code;
}
static void shell_scan_task(void *arg) {
    (void)arg;esp_netif_ip_info_t info;
    esp_netif_t *station=esp_netif_get_handle_from_ifkey("WIFI_STA_DEF");
    bool partial=false;
    if(station && esp_netif_get_ip_info(station,&info)==ESP_OK && info.ip.addr) {
        uint32_t local=ntohl(info.ip.addr),mask=ntohl(info.netmask.addr);
        uint32_t first=(local&mask)+1,last=(local&mask)|~mask;
        if(last-first>512){first=(local&0xffffff00)+1;last=(local&0xffffff00)+255;partial=true;}
        int64_t deadline=now_ms()+120000;
        for(uint32_t ip=first;ip<last;ip++) {
            LOCK();bool stop=!wifi_online || restarting || ota_in_progress || now_ms()>deadline;
            shell_scan_progress=(ip-first)*100/(last-first);UNLOCK();
            if(stop){partial=true;break;}
            if(ip==local || !meter_port_open_wait(ip,12800,900))continue;
            char host[64];snprintf(host,sizeof(host),"%u.%u.%u.%u",(unsigned)(ip>>24),(unsigned)((ip>>16)&255),(unsigned)((ip>>8)&255),(unsigned)(ip&255));
            float grid,maximum;if(shell_read_limits(host,&grid,&maximum)!=200)continue;
            LOCK();unsigned index=shell_found_count;
            if(index<4){strcpy(shell_found_host[index],host);shell_found_grid[index]=grid;shell_found_max[index]=maximum;shell_found_count++;}
            UNLOCK();
        }
    }else partial=true;
    LOCK();shell_scan_partial=partial;shell_scan_progress=100;shell_scan_running=false;UNLOCK();
    vTaskDelete(NULL);
}
static bool shell_scan_start(void) {
    LOCK();bool ready=wifi_online && !restarting && !ota_in_progress && !shell_scan_running &&
        !shelly_scan_running && !meter_preview.running && !huawei_scan_running && !huawei_preview_running;
    if(ready){shell_scan_running=true;shell_scan_progress=shell_found_count=0;shell_scan_partial=false;}UNLOCK();
    if(!ready)return false;
    if(xTaskCreate(shell_scan_task,"shell_scan",6144,NULL,3,NULL)!=pdPASS){LOCK();shell_scan_running=false;UNLOCK();return false;}
    return true;
}
static esp_err_t shell_scan_get(httpd_req_t *req) {
    cJSON *o=cJSON_CreateObject();if(!o)return send_json(req,NULL);
    LOCK();cJSON_AddBoolToObject(o,"running",shell_scan_running);cJSON_AddBoolToObject(o,"partial",shell_scan_partial);
    cJSON_AddNumberToObject(o,"progress",shell_scan_progress);cJSON *rows=cJSON_AddArrayToObject(o,"devices");
    for(unsigned i=0;i<shell_found_count;i++){
        cJSON *row=cJSON_CreateObject();cJSON_AddStringToObject(row,"host",shell_found_host[i]);
        cJSON_AddNumberToObject(row,"grid_limit_a",shell_found_grid[i]);cJSON_AddNumberToObject(row,"max_charge_a",shell_found_max[i]);cJSON_AddItemToArray(rows,row);
    }UNLOCK();return send_json(req,o);
}
static esp_err_t shell_scan_post(httpd_req_t *req) {
    if(!authorized(req))return ESP_OK;
    if(!shell_scan_start())return httpd_resp_send_err(req,HTTPD_400_BAD_REQUEST,"Suche nicht verfuegbar; WLAN oder laufende Suche pruefen.");
    return shell_scan_get(req);
}
static void shell_limits_task(void *arg) {
    (void)arg;int64_t next_read=0,next_car=0,next_search=0;unsigned failures=0,car_failures=0,search_attempts=0;
    uint32_t generation=UINT32_MAX;
    bool previous_limits=false;char previous_host[64]="";
    while(true) {
        LOCK();bool available=wifi_online && !restarting && !ota_in_progress;
        bool car=evcc_control.configured,limits=settings.shell_limits_auto;
        uint32_t turn=evcc_poll_generation;
        char configured_host[64];strcpy(configured_host,settings.shell_setup_host);
        char host[64];strcpy(host,shell_active_host[0]?shell_active_host:settings.shell_setup_host);
        if((!host[0] || failures>=3) && shell_found_count==1){strcpy(host,shell_found_host[0]);}
        if(car)strcpy(host,evcc_host);
        UNLOCK();
        if(turn!=generation){generation=turn;next_car=0;car_failures=0;}
        if(limits!=previous_limits || strcmp(configured_host,previous_host)){
            previous_limits=limits;strcpy(previous_host,configured_host);next_read=next_search=0;failures=search_attempts=0;
        }
        int64_t now=now_ms();
        bool limits_due=limits && now>=next_read,car_due=car && now>=next_car;
        if(available && host[0] && (limits_due || car_due)) {
            float grid=0,maximum=0;evcc_shell_t sample={.state='?',.reason="http_error"};
            int code=shell_read_snapshot(host,&grid,&maximum,car?&sample:NULL);
            LOCK();float capped=fminf(16,maximum);
            bool valid=code==200 && grid>=capped && capped>=settings.min_charge_a;
            bool same=turn==evcc_poll_generation;
            bool same_limits=same && settings.shell_limits_auto==limits && !strcmp(settings.shell_setup_host,configured_host);
            if(car && same && evcc_control.configured) {
                evcc_shell=sample;evcc_shell_http_code=code;
                evcc_shell_at=code==200?now_ms():0;
                car_failures=code==200?0:car_failures+1;
            }
            if(limits_due && same_limits)shell_limits_code=valid?200:code==200?422:code;
            if(limits_due && same_limits && valid) {
                bool changed=settings.grid_limit_a!=grid || settings.max_charge_a!=capped;
                settings.grid_limit_a=grid;settings.max_charge_a=capped;
                settings.manual_current_a=fminf(settings.manual_current_a,capped);
                if(changed){memset(&manual_filter,0,sizeof(manual_filter));memset(&current_calibration,0,sizeof(current_calibration));}
                strcpy(shell_active_host,host);shell_grid_a=grid;shell_max_a=capped;shell_limits_at=now_ms();failures=search_attempts=0;next_search=0;
            }else if(limits_due && same_limits)failures++;
            UNLOCK();
            if(limits_due)next_read=now_ms()+(valid?300000:30000);
            if(car)next_car=now_ms()+(car_failures>=3?15000:5000);
        }
        if(available && limits && !car && (!host[0] || failures>=3) && now>=next_search) {
            if(shell_scan_start()){next_search=now+shell_search_pause(search_attempts);if(search_attempts<2)search_attempts++;}
            LOCK();if(shell_found_count==1 && strcmp(shell_found_host[0],host)){strcpy(shell_active_host,shell_found_host[0]);next_read=0;}UNLOCK();
        }
        vTaskDelay(ticks(1000));
    }
}
