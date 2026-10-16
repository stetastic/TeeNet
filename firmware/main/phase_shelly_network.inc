/* Mechanical feedback is mandatory. Absolute writes and bounded bistable
   pulses are distinct; an uncertain command is never retransmitted. */
static bool phase_output_coil(const char *host,unsigned id,bool writing,bool *on);
static bool phase_output_input(const char *host,unsigned id,bool *closed);
static int phase_shelly_http(const char *host,const char *password,const char *path,char *data,size_t capacity){
    if(!shelly_phase_endpoint_valid(host,64))return 0;
    char url[160];int length=snprintf(url,sizeof(url),"http://%s%s",host,path);
    if(length<=0||(size_t)length>=sizeof(url))return 0;
    http_body_t body={.data=data,.capacity=capacity};data[0]=0;
    esp_http_client_config_t config={.url=url,.timeout_ms=800,.disable_auto_redirect=true,
        .username="admin",.password=password[0]?password:NULL,
        .auth_type=password[0]?HTTP_AUTH_TYPE_DIGEST:HTTP_AUTH_TYPE_NONE,
        .max_authorization_retries=password[0]?1:-1,.event_handler=http_body_event,.user_data=&body};
    esp_http_client_handle_t client=esp_http_client_init(&config);if(!client)return 0;
    esp_err_t err=esp_http_client_perform(client);
    int code=err==ESP_OK?esp_http_client_get_status_code(client):0;
    if(body.overflow||(code==200&&!esp_http_client_is_complete_data_received(client)))code=0;
    esp_http_client_cleanup(client);return code;
}
static bool phase_shelly_validate(const char *host,const char *password,char *body,size_t capacity){
    shelly_phase_identity_t identity;
    return phase_shelly_http(host,password,"/rpc/Shelly.GetDeviceInfo",body,capacity)==200 &&
        shelly_phase_identity(body,&identity)&&identity.profile==settings.phase_shelly_model&&
        identity.inputs>=settings.phase_feedback_count&&
        phase_shelly_http(host,password,"/rpc/Shelly.GetConfig",body,capacity)==200&&
        shelly_phase_config_for(body,settings.phase_feedback_count,settings.phase_shelly_pulse);
}
static bool phase_shelly_modbus_read(const char *host,shelly_phase_feedback_t *out){
    shelly_phase_feedback_t value={0};
    bool ok=phase_output_coil(host,0,false,&value.output)&&phase_output_input(host,0,&value.no_closed)&&
        (settings.phase_feedback_count==1||phase_output_input(host,1,&value.nc_closed));
    if(ok){value.phases=phase_feedback_contacts(value.no_closed,value.nc_closed,settings.phase_feedback_count,
            settings.phase_feedback_closed_is_single);*out=value;}
    return ok;
}
static void phase_shelly_task(void *arg){
    (void)arg;char *body=malloc(4096);if(!body){LOCK();phase_shelly_lost_locked();UNLOCK();vTaskDelete(NULL);return;}
    char host[64],password[65];LOCK();strcpy(host,settings.phase_shelly_host);strcpy(password,settings.phase_shelly_password);UNLOCK();
    int64_t next_config=0;
    for(;;){
        boot_task_seen[4]=(uint32_t)now_ms();
        LOCK();bool online=wifi_online;bool configured=phase_shelly_config_ok;UNLOCK();
        int64_t now=now_ms();
        if(!online){LOCK();phase_shelly_config_ok=false;phase_shelly_lost_locked();UNLOCK();vTaskDelay(ticks(1000));continue;}
        /* Modbus only provides bits, not input mode/inversion/timers. Validate
           the real device over read-only RPC for both switch transports. */
        if(!configured||now>=next_config){
            bool valid=phase_shelly_validate(host,password,body,4096);now=now_ms();
            LOCK();phase_shelly_config_ok=valid;phase_shelly_config_at=valid?now:0;
            if(!valid)phase_shelly_lost_locked();
            UNLOCK();
            next_config=now+30000;
            if(!valid){vTaskDelay(ticks(3000));continue;}
        }
        shelly_phase_feedback_t feedback;
        bool read=settings.phase_actuator_type==3?phase_shelly_modbus_read(host,&feedback):
            phase_shelly_http(host,password,"/rpc/Shelly.GetStatus",body,4096)==200&&
            shelly_phase_feedback_for(body,&feedback,settings.phase_feedback_count,settings.phase_feedback_closed_is_single);
        now=now_ms();
        LOCK();
        bool received=phase_shelly_receive_locked(&feedback,read,now);
        bool issue=received&&phase_shelly_take_pulse_locked(now);
        UNLOCK();
        if(issue){
            char path[80];bool on=settings.phase_shelly_true_three_phase?phase_shelly_goal==3:phase_shelly_goal==1;
            bool ok;
            if(settings.phase_actuator_type==3)ok=phase_output_coil(host,0,true,&on);
            else{
                ok=(settings.phase_shelly_pulse?shelly_phase_pulse_path(path,sizeof(path)):
                    shelly_output_path(path,sizeof(path),"Set",0,on))&&
                    phase_shelly_http(host,password,path,body,4096)==200;
                ok=ok&&(settings.phase_shelly_pulse?shelly_phase_pulse_ack(body):shelly_output_ack(body));
            }
            LOCK();phase_shelly_ack_locked(ok);UNLOCK();
        }
        LOCK();bool switching=phase_state==PHASE_STOPPING||phase_state==PHASE_MOVING||phase_state==PHASE_SETTLING;UNLOCK();
        vTaskDelay(ticks(!read?1500:switching?250:1000));
    }
}
