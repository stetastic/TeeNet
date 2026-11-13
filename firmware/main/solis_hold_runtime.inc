/* Volatile, explicit opt-in. No command from MQTT can enable this function.
   The ioBroker owner lease expires after 15 seconds without a fresh request. */
static bool solis_hold_wanted,solis_hold_release,solis_hold_blocked,solis_hold_busy,solis_hold_manual;
static bool solis_hold_ready,solis_hold_error;
static uint32_t solis_hold_seq;
static int64_t solis_hold_seen,solis_hold_sent;
static char solis_hold_session[17];
static bool solis_hold_link_locked(int64_t now){
    return settings.mqtt_enabled&&mqtt_online&&solis_hold_ready&&
        solis_hold_seen>0&&now-solis_hold_seen<10000;
}
static void solis_hold_init(void){
    snprintf(solis_hold_session,sizeof(solis_hold_session),"%08lx%08lx",
        (unsigned long)esp_random(),(unsigned long)esp_random());
}
static void solis_hold_ack_locked(const char *text){
    cJSON *o=cJSON_ParseWithOpts(text,NULL,true);if(!cJSON_IsObject(o)){cJSON_Delete(o);return;}
    cJSON *ready=cJSON_GetObjectItemCaseSensitive(o,"ready"),*blocked=cJSON_GetObjectItemCaseSensitive(o,"blocked"),
        *busy=cJSON_GetObjectItemCaseSensitive(o,"busy"),*manual=cJSON_GetObjectItemCaseSensitive(o,"manual"),
        *session=cJSON_GetObjectItemCaseSensitive(o,"session"),*seq=cJSON_GetObjectItemCaseSensitive(o,"seq"),
        *ok=cJSON_GetObjectItemCaseSensitive(o,"ok"),*owned=cJSON_GetObjectItemCaseSensitive(o,"owned");
    if(cJSON_IsBool(ready)&&cJSON_IsBool(blocked)&&cJSON_IsBool(busy)&&cJSON_IsBool(manual)&&
       cJSON_IsBool(ok)&&cJSON_IsBool(owned)&&cJSON_IsString(session)&&cJSON_IsNumber(seq)){
        solis_hold_ready=cJSON_IsTrue(ready);solis_hold_seen=now_ms();
        solis_hold_manual=cJSON_IsTrue(manual);
        bool match=!strcmp(session->valuestring,solis_hold_session)&&seq->valuedouble==solis_hold_seq;
        if(!solis_hold_wanted&&!solis_hold_release){solis_hold_blocked=cJSON_IsTrue(blocked);solis_hold_busy=cJSON_IsTrue(busy);}
        if(match){
            solis_hold_blocked=cJSON_IsTrue(blocked);solis_hold_busy=cJSON_IsTrue(busy);
            solis_hold_error=!cJSON_IsTrue(ok);
            if(solis_hold_release&&!cJSON_IsTrue(owned)&&!solis_hold_busy)solis_hold_release=false;
            if(solis_hold_wanted&&solis_hold_error){solis_hold_wanted=false;solis_hold_release=true;solis_hold_seq++;solis_hold_sent=0;}
        }
    }
    cJSON_Delete(o);
}
static void solis_hold_json_locked(cJSON *o,int64_t now){
    cJSON_AddBoolToObject(o,"solis_hold_enabled",personal_solis_enabled);
    if(!personal_solis_enabled)return;
    cJSON_AddBoolToObject(o,"solis_hold_available",solis_hold_link_locked(now));
    cJSON_AddBoolToObject(o,"solis_hold_selected",solis_hold_wanted);
    cJSON_AddBoolToObject(o,"solis_hold_confirmed",solis_hold_link_locked(now)&&solis_hold_blocked);
    cJSON_AddBoolToObject(o,"solis_hold_pending",solis_hold_release||solis_hold_busy||(solis_hold_wanted&&!solis_hold_blocked));
    cJSON_AddBoolToObject(o,"solis_hold_manual",solis_hold_manual);
    cJSON_AddBoolToObject(o,"solis_hold_error",solis_hold_error);
}
static void solis_hold_publish(void){
    char topic[128],body[160];bool issue=false;
    LOCK();int64_t now=now_ms();
    if(solis_hold_wanted&&(!personal_solis_enabled||!solis_hold_link_locked(now)||settings.basic_mode||!settings.battery_protect||
            restarting||ota_in_progress||boot_validation_failed)){
        solis_hold_wanted=false;solis_hold_release=true;solis_hold_seq++;solis_hold_sent=0;
    }
    if((solis_hold_wanted||solis_hold_release)&&mqtt_online&&mqtt_client&&settings.mqtt_enabled&&
            (!solis_hold_sent||now-solis_hold_sent>=3000)){
        snprintf(topic,sizeof(topic),"%s/solis/hold/request",settings.mqtt_prefix);
        snprintf(body,sizeof(body),"{\"session\":\"%s\",\"seq\":%lu,\"hold\":%s,\"sent_at\":%lld}",
            solis_hold_session,(unsigned long)solis_hold_seq,solis_hold_wanted?"true":"false",(long long)time(NULL));
        solis_hold_sent=now;issue=true;
    }
    UNLOCK();
    if(issue)esp_mqtt_client_publish(mqtt_client,topic,body,0,0,0);
}
