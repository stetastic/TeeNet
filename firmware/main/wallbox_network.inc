/* External wallbox meter only; the Shell configuration API is not feedback. */
static bool read_wallbox_network(float out[3],float *total_w) {
    char type[sizeof(settings.wallbox_meter_type)],host[sizeof(settings.wallbox_meter_host)];
    unsigned phases;uint8_t unit;
    LOCK();bool online=wifi_online;strcpy(type,settings.wallbox_meter_type);strcpy(host,settings.wallbox_meter_host);
    phases=settings.charge_phases;unit=settings.xemex_address;UNLOCK();
    if(!online)return false;
    shelly_modbus_reading_t reading;
    if(!strcmp(type,"em24_tcp")){
        em24_reading_t em24;
        if(!read_em24_modbus(host,unit,&em24,2500))return false;
        memcpy(reading.current_a,em24.current_a,sizeof(em24.current_a));
        reading.active_power_w=em24.active_power_w;reading.phases=3;
    }else if(!read_shelly_modbus(host,type,&reading,2500))return false;
    memset(out,0,sizeof(float)*3);
    /* Preserve all independently measured phases, even in one-phase mode.
       Unexpected L2/L3 current must remain visible to the phase interlock. */
    if(reading.phases==3){memcpy(out,reading.current_a,sizeof(float)*3);*total_w=fmaxf(0,reading.active_power_w);}
    else {for(unsigned i=0;i<phases;i++)out[i]=reading.current_a[0];*total_w=fmaxf(0,reading.active_power_w)*phases;}
    return true;
}
