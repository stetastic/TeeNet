/* Read-only submeter. This data never replaces the grid or Wallbox meter. */
static float heatpump_power_w;
static int64_t heatpump_at;
static bool heatpump_task_ready=true;

static bool heatpump_fresh_locked(int64_t now) {
    bool connected=!strcmp(settings.heatpump_source,"mqtt")?mqtt_online:wifi_online;
    return settings.heatpump_enabled&&!settings.basic_mode&&heatpump_task_ready&&connected&&
        fresh(now,heatpump_at,EMS_INPUT_TTL)&&isfinite(heatpump_power_w)&&
        heatpump_power_w>=0&&heatpump_power_w<=50000;
}
static bool heatpump_mqtt_locked(const char *topic,const char *payload,bool retained) {
    if(!settings.heatpump_enabled||settings.basic_mode||!settings.mqtt_enabled||
       strcmp(settings.heatpump_source,"mqtt")||strcmp(topic,settings.heatpump_input_topic)||retained)return false;
    float watts=0;bool available=false;
    bool parsed=heatpump_parse_value(payload,&watts,&available);
    if(parsed&&available){heatpump_power_w=watts;heatpump_at=now_ms();}
    else heatpump_at=0;
    return parsed;
}
static bool heatpump_poll_once(void) {
    char host[64],source[20];
    LOCK();bool active=settings.heatpump_enabled&&!settings.basic_mode&&wifi_online;
    strcpy(host,settings.heatpump_host);strcpy(source,settings.heatpump_source);
    if(!active&&strcmp(source,"mqtt"))heatpump_at=0;
    UNLOCK();
    if(!active||!strcmp(source,"mqtt"))return false;
    shelly_modbus_reading_t reading;
    bool ok=read_shelly_modbus(host,source,&reading,1500)&&isfinite(reading.active_power_w)&&
        reading.active_power_w>=0&&reading.active_power_w<=50000;
    LOCK();
    if(settings.heatpump_enabled&&!settings.basic_mode&&!strcmp(host,settings.heatpump_host)&&
       !strcmp(source,settings.heatpump_source)) {
        if(ok){heatpump_power_w=reading.active_power_w;heatpump_at=now_ms();}
        else heatpump_at=0;
    }
    UNLOCK();return ok;
}
#ifndef TEE_HEATPUMP_TEST
static void heatpump_task(void *arg) {
    (void)arg;
    for(;;){bool ok=heatpump_poll_once();vTaskDelay(ticks(ok?2000:5000));}
}
#endif
