/* Read-only downstream meter; never issue commands to the 3EM. All I/O is
   outside the control task and lock. The meter stays supplied from wallbox L1. */
static void phase_voltage_task(void *arg){
    (void)arg;
    char *body=malloc(4096);
    if(!body){vTaskDelete(NULL);return;}
    for(;;){
        char host[64];uint8_t protocol;
        LOCK();bool online=wifi_online,shared=phase_voltage_shared_locked();strcpy(host,settings.phase_voltage_host);protocol=settings.phase_voltage_protocol;
        bool normal_order=!strcmp(settings.wallbox_meter_type,"shelly_gen2")&&!strcmp(host,settings.wallbox_meter_host)&&!settings.wallbox_shelly_reverse;UNLOCK();
        if(shared){vTaskDelay(ticks(500));continue;} /* The meter task supplies fresh samples once. */
        float logical[3]={0};bool ok=false;
        if(online){
            if(protocol==0){
                uint8_t data[SHELLY_EM_COUNT*2];float physical[3];
                ok=modbus_tcp_read_input(host,SHELLY_EM_START,SHELLY_EM_COUNT,data,sizeof(data),800)&&
                    shelly_modbus_decode_voltage(data,sizeof(data),physical);
                if(ok)for(unsigned i=0;i<3;i++)logical[i]=physical[2-i];
            }else{
                char url[160];snprintf(url,sizeof(url),"http://%s%s",host,protocol==1?"/rpc/EM.GetStatus?id=0":"/status");
                ok=http_get_body(url,body,4096,800)==200&&shelly_phase_voltage_json(body,protocol==2,logical);
            }
        }
        if(ok&&normal_order){float first=logical[0];logical[0]=logical[2];logical[2]=first;}
        LOCK();phase_voltage_sample(&phase_voltage,logical,ok,now_ms());UNLOCK();
        vTaskDelay(ticks(ok?500:1000));
    }
}
