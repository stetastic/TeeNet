/* External wallbox meter only; the Shell configuration API is not feedback. */
static bool read_wallbox_network(wallbox_modbus_session_t *session,float out[3],float *total_w,float voltage[3],bool *voltage_valid) {
    *voltage_valid=false;
    char type[sizeof(settings.wallbox_meter_type)],host[sizeof(settings.wallbox_meter_host)];
    unsigned phases;uint8_t unit;
    LOCK();bool online=wifi_online;strcpy(type,settings.wallbox_meter_type);strcpy(host,settings.wallbox_meter_host);
    phases=settings.charge_phases;unit=settings.xemex_address;uint32_t revision=wallbox_meter_revision;
    bool reversed=settings.wallbox_shelly_reverse&&!strcmp(type,"shelly_gen2");UNLOCK();
    if(!online){wallbox_modbus_session_close(session);return false;}
    if(strcmp(type,"shelly_gen2"))wallbox_modbus_session_close(session);
    shelly_modbus_reading_t reading={0};
    if(!strcmp(type,"em24_tcp")){
        em24_reading_t em24;
        if(!read_em24_modbus(host,unit,&em24,2500))return false;
        memcpy(reading.current_a,em24.current_a,sizeof(em24.current_a));
        reading.active_power_w=em24.active_power_w;reading.phases=3;
    }else if(!strcmp(type,"shelly_gen2")){
        uint8_t data[SHELLY_EM_COUNT*2];
        if(!wallbox_modbus_session_read(session,host,1,revision,SHELLY_EM_START,SHELLY_EM_COUNT,data,sizeof(data),2500))return false;
        if(!shelly_modbus_decode_em(data,sizeof(data),&reading)){wallbox_modbus_session_close(session);return false;}
        if(!reading.voltage_valid)wallbox_modbus_session_close(session);
    }else if(!read_shelly_modbus(host,type,&reading,2500))return false;
    memset(out,0,sizeof(float)*3);
    /* Preserve all independently measured phases, even in one-phase mode.
       Unexpected L2/L3 current must remain visible to the phase interlock. */
    if(reading.phases==3){memcpy(out,reading.current_a,sizeof(float)*3);
        if(reversed){float first=out[0];out[0]=out[2];out[2]=first;}*total_w=fmaxf(0,reading.active_power_w);}
    else {for(unsigned i=0;i<phases;i++)out[i]=reading.current_a[0];*total_w=fmaxf(0,reading.active_power_w)*phases;}
    if(!strcmp(type,"shelly_gen2")&&reading.voltage_valid){
        for(unsigned i=0;i<3;i++)voltage[i]=reading.voltage_v[reversed?2-i:i];
        *voltage_valid=true;
    }
    return true;
}
