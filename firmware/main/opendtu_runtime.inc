static void opendtu_subscribe(esp_mqtt_client_handle_t client){
    if(settings.mqtt_input_source!=2)return;
    char topic[160];
    if(settings.battery_protect){
        const char *keys[]={"battery/stateOfCharge","battery/dataAge","battery/voltage","battery/current"};
        for(unsigned i=0;i<4;i++){snprintf(topic,sizeof(topic),"%s/%s",settings.opendtu_prefix,keys[i]);esp_mqtt_client_subscribe(client,topic,0);}
    }
    if(settings.battery_protect||settings.pv_display_enabled){snprintf(topic,sizeof(topic),"%s/dtu/status",settings.opendtu_prefix);esp_mqtt_client_subscribe(client,topic,0);}
    if(settings.pv_display_enabled){
        esp_mqtt_client_subscribe(client,settings.opendtu_pv_topic,0);
        if(settings.opendtu_pv_valid_topic[0])esp_mqtt_client_subscribe(client,settings.opendtu_pv_valid_topic,0);
    }
}
static void opendtu_apply_locked(int64_t now){
    if(settings.mqtt_input_source!=2)return;
    if(settings.battery_protect&&!(settings.huawei_enabled&&settings.huawei_battery)&&!(settings.sungrow_enabled&&settings.sungrow_battery)){
        battery_soc_at=opendtu_battery_fresh(&opendtu,now)?opendtu.soc_at:0;battery_soc=opendtu.soc;
        float charge,discharge;
        if(opendtu_power(&opendtu,&settings,now,&charge,&discharge)){
            battery_charge_w=charge;battery_discharge_w=discharge;
            battery_charge_at=battery_discharge_at=opendtu.voltage_at<opendtu.current_at?opendtu.voltage_at:opendtu.current_at;
        }else battery_charge_at=battery_discharge_at=0;
    }
    if(settings.pv_display_enabled&&!(settings.huawei_enabled&&settings.huawei_pv)&&!(settings.sungrow_enabled&&settings.sungrow_pv)){
        pv_generation_w=opendtu.pv;pv_generation_at=opendtu_pv_fresh(&opendtu,&settings,now)?opendtu.pv_at:0;
    }
}
static bool opendtu_message(const char *topic,const char *payload,bool retained){
    LOCK();bool ok=opendtu_receive(&opendtu,&settings,topic,payload,retained,now_ms());opendtu_apply_locked(now_ms());UNLOCK();return ok;
}
