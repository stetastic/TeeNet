/* Read-only Modbus TCP, independent task. No socket IO under the control lock. */
static int64_t sungrow_at;
static bool sungrow_registers(int fd,uint8_t unit,uint16_t reg,uint16_t count,uint8_t *data,int64_t budget){
    if(now_ms()>=budget)return false;
    int64_t deadline=now_ms()+1200;if(deadline>budget)deadline=budget;
    uint16_t tx=(uint16_t)esp_random();uint8_t exception;
    uint8_t req[12]={tx>>8,tx&255,0,0,0,6,unit,4,reg>>8,reg&255,count>>8,count&255},head[9];
    if(!socket_exact(fd,req,12,true,deadline)||!socket_exact(fd,head,9,false,deadline)){shutdown(fd,SHUT_RDWR);return false;}
    if(!sungrow_reply_header(head,tx,unit,count,&exception)){if(!exception)shutdown(fd,SHUT_RDWR);return false;}
    bool ok=socket_exact(fd,data,count*2,false,deadline);
    if(!ok)shutdown(fd,SHUT_RDWR);
    if(ok)vTaskDelay(ticks(40)); /* WiNet needs a gap between requests. */
    return ok;
}
static uint32_t sungrow_poll(unsigned *failure_count){
        unsigned failures=*failure_count;
        /* Only copy the reader's small immutable configuration, not a full settings blob. */
        char host[64];uint8_t unit,profile;int port;bool grid,pv,bat,ready;
        LOCK();strcpy(host,settings.sungrow_host);unit=settings.sungrow_unit_id;
        profile=settings.sungrow_profile;port=settings.sungrow_port;
        grid=settings.sungrow_enabled&&!strcmp(settings.house_meter_type,"sungrow")&&settings.zero_feed_enabled;
        pv=settings.sungrow_enabled&&settings.sungrow_pv&&settings.pv_display_enabled;
        bat=settings.sungrow_enabled&&settings.sungrow_battery&&settings.battery_protect;
        ready=wifi_online&&!restarting&&!ota_in_progress;UNLOCK();
        sungrow_reading_t r={0};int64_t grid_at=0,pv_sample_at=0,bat_at=0,at=0;uint32_t interval=5000;
        if(ready&&(grid||pv||bat)){
            int64_t budget=now_ms()+4500;
            int fd=tcp_connect_timeout(host,port,800);uint8_t data[46];
            if(fd>=0){
                if(profile==SUNGROW_IHM){
                    if(sungrow_registers(fd,unit,8154,9,data,budget)){sungrow_ihm_decode(data,18,&r);at=grid_at=pv_sample_at=bat_at=now_ms();}
                }else{
                    if(pv&&sungrow_registers(fd,unit,profile==SUNGROW_SH?5016:5030,2,data,budget)){
                        r.pv_ok=sungrow_power_decode(data,4,false,1,&r.pv_w)&&r.pv_w>=0;at=pv_sample_at=now_ms();
                    }
                    if(profile==SUNGROW_SH&&(grid||bat)&&sungrow_registers(fd,unit,13000,23,data,budget)){
                        sungrow_sh_decode(data,46,&r);at=grid_at=bat_at=now_ms();
                    }else if(profile==SUNGROW_SG_METER&&grid&&sungrow_registers(fd,unit,5082,2,data,budget)){
                        r.grid_ok=sungrow_power_decode(data,4,true,1,&r.grid_w);at=grid_at=now_ms();
                    }
                }
                close(fd);
            }
            LOCK();
            bool same=settings.sungrow_enabled&&settings.sungrow_profile==profile&&settings.sungrow_unit_id==unit&&
                settings.sungrow_port==port&&!strcmp(settings.sungrow_host,host)&&wifi_online&&!restarting&&!ota_in_progress;
            if(same&&(r.grid_ok||r.pv_ok||r.battery_ok))sungrow_at=at;
            if(same&&grid&&!strcmp(settings.house_meter_type,"sungrow")){
                house_current_at=0; /* Inverter output currents are NOT house connection currents. */
                if(r.grid_ok){house_power_w=r.grid_w;house_power_at=grid_at;
                    if(fresh(now_ms(),actual_at,wallbox_meter_ttl())){settings.pv_surplus_a=pv_available_current_locked(now_ms(),r.grid_w);pv_at=grid_at;}
                }
            }
            if(same&&pv&&settings.sungrow_pv&&r.pv_ok){pv_generation_w=r.pv_w;pv_generation_at=pv_sample_at;}
            if(same&&bat&&settings.sungrow_battery&&r.battery_ok){
                battery_soc=r.soc;battery_charge_w=r.charge_w;battery_discharge_w=r.discharge_w;
                battery_soc_at=battery_charge_at=battery_discharge_at=bat_at;
            }
            UNLOCK();
            bool ok=(!grid||r.grid_ok)&&(!pv||r.pv_ok)&&(!bat||r.battery_ok);
            /* Keep successful independent values current even when an optional register is missing. */
            if(ok||r.grid_ok||r.pv_ok||r.battery_ok)failures=0;else if(failures<10)failures++;
            interval=failures>=5?30000:failures>=3?15000:5000;
        }
        *failure_count=failures;return interval;
}
static void sungrow_task(void *arg){
    (void)arg;unsigned failures=0;
    while(true){
        int64_t started=now_ms();uint32_t interval=sungrow_poll(&failures);
        int64_t wait=interval-(now_ms()-started);vTaskDelay(ticks(wait>200?wait:200));
    }
}
