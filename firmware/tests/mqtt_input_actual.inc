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
static bool mqtt_input(const char *suffix,const char *payload) {

    if(!settings.mqtt_enabled) return false;
    /* Retired command/phases and outgoing boolean topics have no input handler. */
    if(settings.mqtt_input_source==2 && (!strncmp(suffix,"input/battery_",14)||!strncmp(suffix,"input/pv_generation",19)))return true;
    if((!settings.battery_protect || (settings.huawei_enabled && settings.huawei_battery)) && !strncmp(suffix,"input/battery_",14))return true;
    if((!settings.pv_display_enabled || (settings.huawei_enabled && settings.huawei_pv)) && !strncmp(suffix,"input/pv_generation",19))return true;
    double value;
    if(!strcmp(suffix,"input/car_soc_valid")) {
        if(strcmp(payload,"false") && strcmp(payload,"0")) return false;
        LOCK(); car_soc_at=0; UNLOCK(); return true;
    }
    if(!strcmp(suffix,"input/car_soc_pct")) {
        if(!parse_number(payload,0,100,&value)) return false;
        LOCK(); if(settings.vehicle_soc_enabled) { car_soc=value; car_soc_at=now_ms(); } else car_soc_at=0; UNLOCK(); return true;
    }
    if(!strcmp(suffix,"input/battery_soc_valid")) {
        if(strcmp(payload,"false") && strcmp(payload,"0")) return false;
        LOCK(); battery_soc_at=0; UNLOCK(); return true;
    }
    if(!strcmp(suffix,"input/battery_soc_pct")) {
        if(!parse_number(payload,0,100,&value)) return false;
        LOCK(); battery_soc=value; battery_soc_at=now_ms(); UNLOCK(); return true;
    }
    if(!strcmp(suffix,"input/pv_generation_valid")) {
        if(strcmp(payload,"false") && strcmp(payload,"0")) return false;
        LOCK(); pv_generation_at=0; UNLOCK(); return true;
    }
    if(!strcmp(suffix,"input/battery_power_valid")) {
        if(strcmp(payload,"false") && strcmp(payload,"0")) return false;
        LOCK(); battery_charge_at=0; battery_discharge_at=0; UNLOCK(); return true;
    }
    if(!strcmp(suffix,"input/pv_generation_w")) {
        if(!parse_number(payload,0,25000,&value)) return false;
        LOCK(); pv_generation_w=value; pv_generation_at=now_ms(); UNLOCK(); return true;
    }
    if(!strcmp(suffix,"input/battery_charge_w")) {
        if(!parse_number(payload,0,12000,&value)) return false;
        LOCK(); battery_charge_w=value; battery_charge_at=now_ms(); UNLOCK(); return true;
    }
    if(!strcmp(suffix,"input/battery_discharge_w")) {
        if(!parse_number(payload,0,12000,&value)) return false;
        LOCK(); battery_discharge_w=value; battery_discharge_at=now_ms(); UNLOCK(); return true;
    }

    if(!strcmp(suffix,"sensor/wallbox_power_w")) {

        if(sdm_meter()) return false;

        if(!parse_number(payload,0,50000,&value)) return false;

        LOCK(); if(!network_wallbox_meter()){wallbox_w=value; power_at=now_ms();} UNLOCK(); return true;

    }



    if(strncmp(suffix,"command/",8)) return false;

    const char *key=suffix+8; cJSON *o=cJSON_CreateObject(); if(!o) return false; bool parsed=true;

    if(!strcmp(key,"mode")) {
        control_mode_t mode;parsed=parse_mode(payload,&mode) && mode!=MODE_GRID_LIMIT;
        if(parsed){cJSON_AddStringToObject(o,key,payload);cJSON_AddBoolToObject(o,"enabled",mode!=MODE_OFF);}
    }

    else if(!strcmp(key,"enabled") || !strcmp(key,"battery_use") || !strcmp(key,"battery_start_use")) {

        parsed=!strcmp(payload,"true") || !strcmp(payload,"false") || !strcmp(payload,"1") || !strcmp(payload,"0");

        cJSON_AddBoolToObject(o,key,!strcmp(payload,"true") || !strcmp(payload,"1"));

    } else if(!strcmp(key,"power_kw")) {
        parsed=parse_number(payload,0,11,&value);
        if(parsed){
            double half=round(value*2)/2;
            unsigned phases=settings.phase_switch_enabled?(value>0 && value<=3.5?1:3):settings.fixed_charge_phases;
            double minimum=phases==1?2.0:6.0;
            parsed=(value==0 || (value>=minimum && (phases==3 || value<=3.5) && fabs(value-half)<.001)) &&
                half<=settings.max_charge_a*phases*settings.nominal_v*settings.power_factor/1000.0+.05;
            if(parsed){
                double current=value==0?0:fmax(settings.min_charge_a,
                    half*1000/(phases*settings.nominal_v*settings.power_factor));
                parsed=current<=settings.max_charge_a+.001;
                if(parsed){cJSON_AddStringToObject(o,"mode","manual");cJSON_AddNumberToObject(o,"current_a",fmin(current,settings.max_charge_a));cJSON_AddNumberToObject(o,"manual_phases",phases);cJSON_AddBoolToObject(o,"enabled",value>0);}
            }
        }
    } else if(!strcmp(key,"current_a") || !strcmp(key,"pv_surplus_a")) {

        parsed=parse_number(payload,0,63,&value);
        if(parsed) {
            cJSON_AddNumberToObject(o,key,value);
            if(!strcmp(key,"current_a")) {
                cJSON_AddStringToObject(o,"mode","manual");
                cJSON_AddBoolToObject(o,"enabled",true);
            }
        }

    } else parsed=false;

    bool ok=parsed && apply_control(o); cJSON_Delete(o); return ok;

}

#include "opendtu_runtime.inc"
static void mqtt_event(void *arg,esp_event_base_t base,int32_t id,void *data) {

    esp_mqtt_event_handle_t e=data;

    static char topic[192],payload[512]; static int received,expected;static bool retained;

    if(id==MQTT_EVENT_CONNECTED) {
        LOCK();if(settings.phase_output_transport==0)phase_mqtt_seen_connection=true;UNLOCK();

        LOCK(); mqtt_online=true; UNLOCK(); char subscription[192];

        snprintf(subscription,sizeof(subscription),"%s/command/#",settings.mqtt_prefix); esp_mqtt_client_subscribe(e->client,subscription,1);

        if(!sdm_meter() && !network_wallbox_meter()) { snprintf(subscription,sizeof(subscription),"%s/sensor/wallbox_power_w",settings.mqtt_prefix); esp_mqtt_client_subscribe(e->client,subscription,1); }

        snprintf(subscription,sizeof(subscription),"%s/input/+",settings.mqtt_prefix); esp_mqtt_client_subscribe(e->client,subscription,1);
        opendtu_subscribe(e->client);
        if(settings.heatpump_enabled&&!settings.basic_mode&&!strcmp(settings.heatpump_source,"mqtt"))
            esp_mqtt_client_subscribe(e->client,settings.heatpump_input_topic,1);


        if(!settings.mqtt_state_json) { snprintf(subscription,sizeof(subscription),"%s/state",settings.mqtt_prefix); esp_mqtt_client_enqueue(e->client,subscription,"",0,1,1,false); }

        snprintf(subscription,sizeof(subscription),"%s/availability",settings.mqtt_prefix);

        esp_mqtt_client_enqueue(e->client,subscription,"online",0,1,1,false);
        homeassistant_discovery(settings.homeassistant_enabled);

    } else if(id==MQTT_EVENT_DISCONNECTED) {

        LOCK();if(settings.phase_output_transport==0&&phase_mqtt_seen_connection)phase_mqtt_cancel_locked(true);memset(&opendtu,0,sizeof(opendtu)); mqtt_online=false; if(!strcmp(settings.heatpump_source,"mqtt"))heatpump_at=0; if(!(settings.huawei_enabled && settings.huawei_battery)){battery_soc_at=0;battery_charge_at=0;battery_discharge_at=0;} car_soc_at=0; if(!(settings.huawei_enabled && settings.huawei_pv))pv_generation_at=0; if(!sdm_meter() && !network_wallbox_meter()) power_at=0; if(!settings.zero_feed_enabled) pv_at=0; UNLOCK(); expected=received=0;

    } else if(id==MQTT_EVENT_DATA) {

        if(e->current_data_offset==0) {

            received=expected=0;

            if(e->topic_len<=0 || e->topic_len>=(int)sizeof(topic) || e->total_data_len<=0 || e->total_data_len>=(int)sizeof(payload)) {

                LOCK(); mqtt_rejected++; UNLOCK(); return;

            }

            memcpy(topic,e->topic,e->topic_len); topic[e->topic_len]=0; expected=e->total_data_len;retained=e->retain;

        }

        if(!expected || e->current_data_offset!=received || e->data_len<0 || received+e->data_len>expected) { expected=0; return; }

        memcpy(payload+received,e->data,e->data_len); received+=e->data_len;

        if(received==expected) {

            payload[received]=0; expected=0; char prefix[80]; snprintf(prefix,sizeof(prefix),"%s/",settings.mqtt_prefix);

            bool ok=false;
            if(!memchr(payload,0,received)) {
                if(settings.heatpump_enabled&&!strcmp(settings.heatpump_source,"mqtt")&&!strcmp(topic,settings.heatpump_input_topic)){
                    LOCK();ok=heatpump_mqtt_locked(topic,payload,retained);UNLOCK();
                }
                else if(settings.mqtt_input_source==2 && opendtu_message(topic,payload,retained))ok=true;
                else if(!retained&&!strncmp(topic,prefix,strlen(prefix)))ok=mqtt_input(topic+strlen(prefix),payload);
            }

            if(!ok) { LOCK(); mqtt_rejected++; UNLOCK(); }

        }

    }

}

