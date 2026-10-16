/* Each output is sent once, never queued/replayed. No mechanical feedback. */
static bool phase_output_direct_read(uint8_t transport,const char *host,unsigned id,bool *on);
static bool phase_output_direct_write(uint8_t transport,const char *host,unsigned id,bool on);
static bool phase_output_direct_read_auth(uint8_t transport,const char *host,unsigned id,const char *password,bool *on);
static bool phase_output_direct_write_auth(uint8_t transport,const char *host,unsigned id,const char *password,bool on);
static void phase_mqtt_publish_once(void){
        if(xSemaphoreTake(phase_mqtt_send_lock,0)==pdTRUE){
            char topic[128];uint8_t goal=0;uint32_t generation=0;bool on=false;
            char host[64],password[65];uint8_t transport,id;
            LOCK();transport=settings.phase_output_transport;id=settings.phase_output_switch_id;
            strcpy(host,settings.phase_output_host);strcpy(password,settings.phase_output_password);
            bool read=transport>0&&wifi_online&&now_ms()>=phase_output_health_due;UNLOCK();
            if(read){
                bool measured=false,ok=phase_output_direct_read_auth(transport,host,id,password,&measured);
                LOCK();phase_output_health_locked(ok,measured,now_ms());
                phase_output_health_due=now_ms()+(ok?1000:2000);UNLOCK();
            }
            LOCK();
            bool issue=(transport>0||mqtt_client)&&phase_mqtt_take_locked(now_ms(),&goal,&generation);
            if(issue&&transport==0&&!phase_mqtt_output_topic(&settings,topic,sizeof(topic))){
                phase_mqtt_sent_locked(goal,generation,false,now_ms());issue=false;
            }
            if(issue)on=phase_mqtt_output_value(&settings,goal);
            UNLOCK();
            if(issue){
                bool success=transport>0?phase_output_direct_write_auth(transport,host,id,password,on):
                    esp_mqtt_client_publish(mqtt_client,topic,on?"true":"false",0,0,0)>=0;
                LOCK();phase_mqtt_sent_locked(goal,generation,success,now_ms());UNLOCK();
            }
            memset(password,0,sizeof(password));
            xSemaphoreGive(phase_mqtt_send_lock);
        }
}
static void phase_mqtt_task(void *arg){
    (void)arg;for(;;){
        boot_task_seen[4]=(uint32_t)now_ms();phase_mqtt_publish_once();vTaskDelay(ticks(100));}
}
