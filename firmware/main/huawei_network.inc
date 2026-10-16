/* One optional reader, one socket, bounded deadlines. No inverter writes. */
static SemaphoreHandle_t huawei_io;
static int64_t huawei_at;
static char huawei_model[31];
static bool huawei_scan_running,huawei_scan_partial,huawei_preview_running;
static unsigned huawei_scan_progress,huawei_found_count;
static int64_t huawei_scan_started;
typedef struct {char host[64],model[31];uint8_t unit;} huawei_found_t;
static huawei_found_t huawei_found[8];
typedef struct {
    char host[64],model[31];uint8_t unit;
    bool connected,grid_ok,current_ok,pv_ok,battery_ok;
    float grid_w,grid_a[3],pv_w,soc,charge_w,discharge_w;
    int64_t grid_at,current_at,pv_at,battery_at;
} huawei_reading_t;
static huawei_reading_t huawei_preview;

static bool huawei_registers(int sock,uint8_t unit,uint16_t reg,uint16_t count,uint8_t *data,
                             int64_t budget,uint8_t *exception){
    *exception=0;if(now_ms()>=budget)return false;
    int64_t deadline=now_ms()+2000;if(deadline>budget)deadline=budget;
    uint16_t tx=(uint16_t)esp_random();
    uint8_t req[12]={tx>>8,tx&255,0,0,0,6,unit,3,reg>>8,reg&255,count>>8,count&255},head[9];
    if(!socket_exact(sock,req,12,true,deadline)||!socket_exact(sock,head,9,false,deadline)){
        shutdown(sock,SHUT_RDWR);return false;
    }
    if(!huawei_reply_header(head,tx,unit,count,exception)){
        /* A valid Modbus exception is complete; a broken stream cannot be reused. */
        if(!*exception)shutdown(sock,SHUT_RDWR);
        return false;
    }
    bool ok=socket_exact(sock,data,count*2,false,deadline);
    if(!ok)shutdown(sock,SHUT_RDWR);
    if(ok)vTaskDelay(ticks(100));
    return ok;
}
static int huawei_open(const char *host,int64_t *budget){
    *budget=now_ms()+8000;
    int sock=tcp_connect_timeout(host,502,1000);
    /* The Smart Dongle needs a short settling time after TCP connect. */
    if(sock>=0)vTaskDelay(ticks(1500));
    return sock;
}
static bool huawei_probe(const char *host,uint8_t unit,char model[31]){
    if(!huawei_io||xSemaphoreTake(huawei_io,ticks(100))!=pdTRUE)return false;
    int64_t budget;int sock=huawei_open(host,&budget);uint8_t data[30],exception;
    bool ok=sock>=0&&huawei_registers(sock,unit,30000,15,data,budget,&exception)&&huawei_model_decode(data,30,model);
    if(sock>=0)close(sock);
    xSemaphoreGive(huawei_io);return ok;
}
static bool huawei_read(const char *host,uint8_t unit,bool grid,bool pv,bool battery,huawei_reading_t *r){
    memset(r,0,sizeof(*r));strncpy(r->host,host,sizeof(r->host)-1);r->unit=unit;
    if(!huawei_io||xSemaphoreTake(huawei_io,ticks(100))!=pdTRUE)return false;
    int64_t budget;int sock=huawei_open(host,&budget);uint8_t data[52],exception;
    if(sock>=0&&huawei_registers(sock,unit,30000,15,data,budget,&exception)&&huawei_model_decode(data,30,r->model)){
        r->connected=true;
        if(grid){
            bool extended=huawei_registers(sock,unit,37100,26,data,budget,&exception);
            if(extended){
                r->grid_ok=huawei_grid_decode(data,52,&r->grid_w);
                r->current_ok=huawei_grid_currents_decode(data,52,r->grid_a);
            }else if(exception==2&&huawei_registers(sock,unit,37100,15,data,budget,&exception)){
                /* Older profiles may still supply total power for PV, but
                   never qualify for phase protection without meter type. */
                r->grid_ok=huawei_grid_decode(data,30,&r->grid_w);
            }
            if(r->grid_ok)r->grid_at=now_ms();
            if(r->current_ok)r->current_at=now_ms();
        }
        if(pv&&huawei_registers(sock,unit,32064,2,data,budget,&exception)&&huawei_power_decode(data,&r->pv_w)&&r->pv_w>=0){
            r->pv_ok=true;r->pv_at=now_ms();
        }
        if(battery){
            bool aggregate=huawei_registers(sock,unit,37760,7,data,budget,&exception);
            if(aggregate)r->battery_ok=huawei_battery_decode(data,14,true,&r->soc,&r->charge_w,&r->discharge_w);
            /* Legacy single-storage profile only when aggregate registers are absent.
               Never mask a present aggregate battery's offline/fault status. */
            else if(exception==2&&huawei_registers(sock,unit,37000,5,data,budget,&exception))
                r->battery_ok=huawei_battery_decode(data,10,false,&r->soc,&r->charge_w,&r->discharge_w);
            if(r->battery_ok)r->battery_at=now_ms();
        }
    }
    if(sock>=0)close(sock);
    xSemaphoreGive(huawei_io);return r->connected;
}
static void huawei_task(void *arg){
    (void)arg;unsigned failures=0;
    while(true){
        int64_t started=now_ms();char host[64];uint8_t unit;bool grid,pv,battery,zero_feed,grid_guard;
        LOCK();strcpy(host,settings.huawei_host);unit=settings.huawei_unit_id;
        zero_feed=settings.zero_feed_enabled;grid_guard=settings.grid_guard_enabled;
        grid=settings.huawei_enabled&&!strcmp(settings.house_meter_type,"huawei")&&(zero_feed||grid_guard);
        pv=settings.huawei_enabled&&settings.huawei_pv&&settings.pv_display_enabled;
        battery=settings.huawei_enabled&&settings.huawei_battery&&settings.battery_protect;
        bool ready=wifi_online&&!restarting&&!ota_in_progress&&!huawei_scan_running&&!huawei_preview_running;
        UNLOCK();
        uint32_t interval=5000;
        if(ready&&(grid||pv||battery)){
            huawei_reading_t r;huawei_read(host,unit,grid,pv,battery,&r);
            LOCK();
            /* A save/OTA during the read must not publish an old source. */
            bool same=settings.huawei_enabled&&settings.huawei_unit_id==unit&&!strcmp(settings.huawei_host,host)&&wifi_online&&!restarting&&!ota_in_progress;
            if(same&&r.connected){strcpy(huawei_model,r.model);huawei_at=now_ms();}
            if(same&&grid&&!strcmp(settings.house_meter_type,"huawei")&&r.grid_ok){
                house_power_w=r.grid_w;house_power_at=r.grid_at;
                if(settings.zero_feed_enabled&&fresh(now_ms(),actual_at,EMS_METER_TTL)){
                    settings.pv_surplus_a=pv_available_current_locked(now_ms(),r.grid_w);pv_at=r.grid_at;
                }
            }
            if(same&&grid&&!strcmp(settings.house_meter_type,"huawei")){
                if(r.current_ok){memcpy(house_a,r.grid_a,sizeof(house_a));house_current_at=r.current_at;}
                else if(r.connected)house_current_at=0;
                if(r.connected&&!r.grid_ok)house_power_at=0;
            }
            if(same&&pv&&settings.huawei_pv&&settings.pv_display_enabled&&r.pv_ok){pv_generation_w=r.pv_w;pv_generation_at=r.pv_at;}
            if(same&&battery&&settings.huawei_battery&&settings.battery_protect&&r.battery_ok){
                battery_soc=r.soc;battery_soc_at=r.battery_at; battery_charge_w=r.charge_w;battery_discharge_w=r.discharge_w;
                battery_charge_at=battery_discharge_at=r.battery_at;
            }
            UNLOCK();
            /* A missing optional battery must not age out a working grid meter. */
            bool ok=r.connected&&((grid&&((zero_feed&&r.grid_ok)||
                (grid_guard&&r.current_ok)))||(pv&&r.pv_ok)||(battery&&r.battery_ok));
            if(ok)failures=0;else if(failures<10)failures++;
            interval=failures>=5?30000:failures>=3?15000:5000;
        }else if(!(grid||pv||battery))failures=0;
        int64_t wait=interval-(now_ms()-started);vTaskDelay(ticks(wait>200?wait:200));
    }
}
