/* Generated actual publisher plus old snapshot selection as comparison; do not edit. */
static void publish_value(const char *name,cJSON *value) {

    char topic[160]; snprintf(topic,sizeof(topic),"%s/sensor/%s",settings.mqtt_prefix,name);

    char *body=value?cJSON_PrintUnformatted(value):NULL;

    /* QoS 0 needs store=true with enqueue; otherwise ESP-MQTT drops it. */
    esp_mqtt_client_enqueue(mqtt_client,topic,body?body:"null",0,0,0,true); free(body);

}

static void mqtt_state(void) {

    LOCK(); bool online=mqtt_online,full_json=settings.mqtt_state_json; UNLOCK();

    if(!online || !mqtt_client || esp_mqtt_client_get_outbox_size(mqtt_client)>8000) return;

    /* HTTP and MQTT must not retain two large status trees at the same time
       on the Atom without PSRAM. Skipping one periodic publish is harmless. */
    if(xSemaphoreTake(status_snapshot_lock,0)!=pdTRUE)return;
    cJSON *o=full_json?status_json_profile(false,true):mqtt_status_snapshot();if(!o){xSemaphoreGive(status_snapshot_lock);return;}

    if(full_json) {

        char *body=json_print_compact(o); char topic[100]; snprintf(topic,sizeof(topic),"%s/state",settings.mqtt_prefix);

        if(body) esp_mqtt_client_enqueue(mqtt_client,topic,body,0,1,1,false);

        free(body);

    }

    /* Publish useful TeeNet outputs only. ioBroker-origin values are already there. */
    const char *names[]={"estimate_w","target_current_a","current_a","manual_power_kw","house_power_w","meter_ok","house_meter_ok","wallbox_ok","enabled","mode","block_reason","battery_use","battery_start_use","charge_stop_latched"};

    for(unsigned i=0;i<sizeof(names)/sizeof(names[0]);i++) {

        cJSON *value=cJSON_GetObjectItemCaseSensitive(o,names[i]);

        if(cJSON_IsString(value)) {

            char topic[160]; snprintf(topic,sizeof(topic),"%s/sensor/%s",settings.mqtt_prefix,names[i]);

            esp_mqtt_client_enqueue(mqtt_client,topic,value->valuestring,0,0,0,true);

        } else publish_value(names[i],value);

    }

    if(settings.heatpump_enabled&&!settings.basic_mode&&settings.heatpump_publish_enabled){
        cJSON *value=cJSON_GetObjectItemCaseSensitive(o,"heatpump_w");char watts[32];
        if(cJSON_IsNumber(value))snprintf(watts,sizeof(watts),"%.0f",value->valuedouble);
        else strcpy(watts,"unavailable");
        esp_mqtt_client_enqueue(mqtt_client,settings.heatpump_output_topic,watts,0,0,0,true);
    }

    static bool phase_topics_published;
    if(phase_mqtt_allowed || phase_topics_published) {
        const char *phase_names[]={"phase_mqtt_allowed","charge_phases"};
        for(unsigned i=0;i<sizeof(phase_names)/sizeof(phase_names[0]);i++)
            publish_value(phase_names[i],cJSON_GetObjectItemCaseSensitive(o,phase_names[i]));
    }
    phase_topics_published=phase_mqtt_allowed;

    if(settings.homeassistant_enabled) {
        const char *homeassistant_values[]={"battery_soc_pct","pv_generation_w","battery_charge_w","battery_discharge_w"};
        for(unsigned i=0;i<sizeof(homeassistant_values)/sizeof(homeassistant_values[0]);i++)
            publish_value(homeassistant_values[i],cJSON_GetObjectItemCaseSensitive(o,homeassistant_values[i]));
    }

    static unsigned energy_cycle=0;
    if(++energy_cycle>=3) {
        energy_cycle=0;
        const char *keys[]={"total_kwh","today_kwh","today_cost"};
        for(int k=0;k<3;k++) {
            char name[64]; snprintf(name,sizeof(name),"energy/estimate_%s",keys[k]);
            publish_value(name,cJSON_GetObjectItem(cJSON_GetArrayItem(cJSON_GetObjectItem(o,"energy"),0),keys[k]));
        }
    }

    cJSON_Delete(o);
    xSemaphoreGive(status_snapshot_lock);
}

static void mqtt_state_full_fixture(void) {

    LOCK(); bool online=mqtt_online,full_json=settings.mqtt_state_json; UNLOCK();

    if(!online || !mqtt_client || esp_mqtt_client_get_outbox_size(mqtt_client)>8000) return;

    /* HTTP and MQTT must not retain two large status trees at the same time
       on the Atom without PSRAM. Skipping one periodic publish is harmless. */
    if(xSemaphoreTake(status_snapshot_lock,0)!=pdTRUE)return;
    cJSON *o=status_json_profile(false,full_json);if(!o){xSemaphoreGive(status_snapshot_lock);return;}

    if(full_json) {

        char *body=json_print_compact(o); char topic[100]; snprintf(topic,sizeof(topic),"%s/state",settings.mqtt_prefix);

        if(body) esp_mqtt_client_enqueue(mqtt_client,topic,body,0,1,1,false);

        free(body);

    }

    /* Publish useful TeeNet outputs only. ioBroker-origin values are already there. */
    const char *names[]={"estimate_w","target_current_a","current_a","manual_power_kw","house_power_w","meter_ok","house_meter_ok","wallbox_ok","enabled","mode","block_reason","battery_use","battery_start_use","charge_stop_latched"};

    for(unsigned i=0;i<sizeof(names)/sizeof(names[0]);i++) {

        cJSON *value=cJSON_GetObjectItemCaseSensitive(o,names[i]);

        if(cJSON_IsString(value)) {

            char topic[160]; snprintf(topic,sizeof(topic),"%s/sensor/%s",settings.mqtt_prefix,names[i]);

            esp_mqtt_client_enqueue(mqtt_client,topic,value->valuestring,0,0,0,true);

        } else publish_value(names[i],value);

    }

    if(settings.heatpump_enabled&&!settings.basic_mode&&settings.heatpump_publish_enabled){
        cJSON *value=cJSON_GetObjectItemCaseSensitive(o,"heatpump_w");char watts[32];
        if(cJSON_IsNumber(value))snprintf(watts,sizeof(watts),"%.0f",value->valuedouble);
        else strcpy(watts,"unavailable");
        esp_mqtt_client_enqueue(mqtt_client,settings.heatpump_output_topic,watts,0,0,0,true);
    }

    static bool phase_topics_published;
    if(phase_mqtt_allowed || phase_topics_published) {
        const char *phase_names[]={"phase_mqtt_allowed","charge_phases"};
        for(unsigned i=0;i<sizeof(phase_names)/sizeof(phase_names[0]);i++)
            publish_value(phase_names[i],cJSON_GetObjectItemCaseSensitive(o,phase_names[i]));
    }
    phase_topics_published=phase_mqtt_allowed;

    if(settings.homeassistant_enabled) {
        const char *homeassistant_values[]={"battery_soc_pct","pv_generation_w","battery_charge_w","battery_discharge_w"};
        for(unsigned i=0;i<sizeof(homeassistant_values)/sizeof(homeassistant_values[0]);i++)
            publish_value(homeassistant_values[i],cJSON_GetObjectItemCaseSensitive(o,homeassistant_values[i]));
    }

    static unsigned energy_cycle=0;
    if(++energy_cycle>=3) {
        energy_cycle=0;
        const char *keys[]={"total_kwh","today_kwh","today_cost"};
        for(int k=0;k<3;k++) {
            char name[64]; snprintf(name,sizeof(name),"energy/estimate_%s",keys[k]);
            publish_value(name,cJSON_GetObjectItem(cJSON_GetArrayItem(cJSON_GetObjectItem(o,"energy"),0),keys[k]));
        }
    }

    cJSON_Delete(o);
    xSemaphoreGive(status_snapshot_lock);
}

