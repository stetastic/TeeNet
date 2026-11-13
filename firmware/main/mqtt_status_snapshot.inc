/* MQTT scalar topics need only these values. Avoid building the dashboard's
   UI/settings trees on every publish when the optional full JSON is off.
   Keep all optional topic values so existing publish conditions remain intact. */
static cJSON *mqtt_status_snapshot(void){
    cJSON *o=cJSON_CreateObject();if(!o)return NULL;
    int64_t epoch,midnight;uint32_t date=calendar(&epoch,&midnight);
    LOCK();int64_t now=now_ms();
#define MN(parent,key,value) do{if(!cJSON_AddNumberToObject(parent,key,value))goto failed;}while(0)
#define MB(key,value) do{if(!cJSON_AddBoolToObject(o,key,value))goto failed;}while(0)
#define MS(key,value) do{if(!cJSON_AddStringToObject(o,key,value))goto failed;}while(0)
#define MV(parent,key,value,valid) do{double v_=(value);if((valid)&&isfinite(v_)){MN(parent,key,v_);}else if(!cJSON_AddNullToObject(parent,key))goto failed;}while(0)
    bool meter_ok=feedback_ready(now)&&fresh(now,meter_at,wallbox_meter_ttl());
    MV(o,"estimate_w",estimated_charge_power(&settings,meter_a),meter_ok);
    MN(o,"target_current_a",target_locked(now));MN(o,"current_a",settings.manual_current_a);
    MN(o,"manual_power_kw",settings.manual_current_a*ems_phase_voltage_sum(&settings,settings.manual_phases)*settings.power_factor/1000);
    MV(o,"house_power_w",house_power_w,fresh(now,house_power_at,house_meter_ttl()));
    MB("meter_ok",meter_ok);
    MB("house_meter_ok",house_power_capable()?fresh(now,house_power_at,house_meter_ttl()):fresh(now,house_current_at,house_meter_ttl()));
    MB("wallbox_ok",fresh(now,wallbox_at,ems_param_ms(EP_SHELL_TTL)));
    MB("enabled",settings.enabled);MS("mode",mode_name(settings.mode));MS("block_reason",block_reason_locked(now));
    MB("battery_use",battery_use);MB("battery_start_use",battery_start_use);MB("charge_stop_latched",charge_guard.latched);
    MV(o,"heatpump_w",heatpump_power_w,heatpump_fresh_locked(now));
    MB("phase_mqtt_allowed",phase_mqtt_allowed);MN(o,"charge_phases",settings.charge_phases);
    MV(o,"battery_soc_pct",battery_soc,fresh(now,battery_soc_at,EMS_INPUT_TTL));
    MV(o,"pv_generation_w",pv_generation_w,settings.pv_display_enabled&&fresh(now,pv_generation_at,EMS_INPUT_TTL));
    battery_power_t battery_power=battery_power_locked(now);
    MV(o,"battery_charge_w",battery_power.charge_w,battery_power.fresh);
    MV(o,"battery_discharge_w",battery_power.discharge_w,battery_power.fresh);
    cJSON *sources=cJSON_AddArrayToObject(o,"energy");if(!sources)goto failed;
    cJSON *item=cJSON_CreateObject();if(!item)goto failed;
    if(!json_add_owned_to_array(sources,item))goto failed;
    double today=0;uint64_t covered=0;
    for(int d=0;d<EMS_DAYS;d++)if(energy.store.days[d].date==date&&date){today=energy.store.days[d].wh[0];covered=energy.store.days[d].covered_ms[0];}
    MN(item,"total_kwh",fmax(0,energy.store.total_wh[0]-energy_reset_wh[0])/1000);
    MV(item,"today_kwh",today/1000,date!=0&&(covered>0||today>0));
    MV(item,"today_cost",today/1000*settings.price_kwh,date!=0&&covered>0);
#undef MN
#undef MB
#undef MS
#undef MV
    UNLOCK();return o;
failed:
    UNLOCK();cJSON_Delete(o);return NULL;
}
