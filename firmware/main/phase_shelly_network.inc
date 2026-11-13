/* Position comes from optional contacts or independent downstream voltage. Absolute writes and bounded bistable
   pulses are distinct; an uncertain command is never retransmitted. */
static bool phase_output_coil(const char *host,unsigned id,bool writing,bool *on);
static unsigned phase_shelly_socket_count(void){
    /* F_GETFL reads only socket flags in this SDK: no allocation, network I/O
       or TCP/IP task wait. SO_TYPE instead queues a TCP/IP callback. This is
       a best-effort FD census, not a count of closed TCP TIME_WAIT PCBs. */
    int saved_errno=errno;unsigned count=0;
    for(int fd=LWIP_SOCKET_OFFSET;fd<LWIP_SOCKET_OFFSET+CONFIG_LWIP_MAX_SOCKETS;fd++)
        if(fcntl(fd,F_GETFL,0)>=0)count++;
    errno=saved_errno;return count;
}
static bool phase_shelly_missing_error(esp_err_t err){
    /* Header/parser/configuration errors are deliberately not in this list. */
    return err==ESP_ERR_HTTP_CONNECT||err==ESP_ERR_HTTP_WRITE_DATA||
        err==ESP_ERR_HTTP_EAGAIN||err==ESP_ERR_HTTP_CONNECTION_CLOSED||
        err==ESP_ERR_TIMEOUT||err==ESP_ERR_NO_MEM;
}
static int phase_shelly_http(const char *host,const char *password,const char *path,char *data,size_t capacity){
    LOCK();phase_shelly_http_diag_valid=false;phase_shelly_http_resources_valid=false;UNLOCK();
    if(!shelly_phase_endpoint_valid(host,64))return -1;
    char url[160];int length=snprintf(url,sizeof(url),"http://%s%s",host,path);
    if(length<=0||(size_t)length>=sizeof(url))return -1;
    http_body_t body={.data=data,.capacity=capacity};data[0]=0;
    esp_http_client_config_t config={.url=url,.timeout_ms=800,.disable_auto_redirect=true,
        .username="admin",.password=password[0]?password:NULL,
        .auth_type=password[0]?HTTP_AUTH_TYPE_DIGEST:HTTP_AUTH_TYPE_NONE,
        .max_authorization_retries=password[0]?1:-1,.event_handler=http_body_event,.user_data=&body};
    esp_http_client_handle_t client=esp_http_client_init(&config);if(!client)return 0;
    esp_err_t err=esp_http_client_perform(client);
    int status=esp_http_client_get_status_code(client),code=-1;
    int socket_error=esp_http_client_get_errno(client);
    bool complete=esp_http_client_is_complete_data_received(client);
    /* A known rejection remains invalid even if its body later times out.
       Only missing/truncated transport retains the old TTL; never overflow,
       complete invalid data or a configuration/protocol error. */
    if(!body.overflow){
        if(status>=100&&status!=200)code=status;
        else if(err==ESP_OK&&status==200)code=complete?200:0;
        /* IDF reports complete before headers when both byte counters are
           zero. Without an HTTP status this does not prove a response. */
        else if((status<100||!complete)&&phase_shelly_missing_error(err))code=0;
    }
    unsigned sockets=0;size_t free_internal=0,largest_internal=0;
    if(code!=200){
        /* Capture before cleanup releases the failed client's resources.
           Never run the census or platform heap queries under state_lock. */
        free_internal=heap_caps_get_free_size(MALLOC_CAP_INTERNAL|MALLOC_CAP_8BIT);
        largest_internal=heap_caps_get_largest_free_block(MALLOC_CAP_INTERNAL|MALLOC_CAP_8BIT);
        sockets=phase_shelly_socket_count();
    }
    LOCK();phase_shelly_http_error=err;phase_shelly_http_status=status;
    phase_shelly_http_errno=socket_error;phase_shelly_http_diag_valid=true;
    phase_shelly_http_resources_valid=code!=200;
    phase_shelly_http_open_sockets=sockets;phase_shelly_http_socket_capacity=CONFIG_LWIP_MAX_SOCKETS;
    phase_shelly_http_internal_free=free_internal;phase_shelly_http_internal_largest=largest_internal;UNLOCK();
    esp_http_client_cleanup(client);return code;
}
static int phase_shelly_validate(const char *host,const char *password,char *body,size_t capacity){
    shelly_phase_identity_t identity;
    int code=phase_shelly_http(host,password,"/rpc/Shelly.GetDeviceInfo",body,capacity);
    if(code!=200)return code==0?0:-1;
    if(!shelly_phase_identity(body,&identity)||identity.profile!=settings.phase_shelly_model)return -1;
    code=phase_shelly_http(host,password,"/rpc/Shelly.GetConfig",body,capacity);
    if(code!=200)return code==0?0:-1;
    return shelly_phase_config(body)?1:-1;
}
static bool phase_shelly_modbus_read(const char *host,shelly_phase_feedback_t *out){
    shelly_phase_feedback_t value={0};
    bool ok=phase_output_coil(host,0,false,&value.output);
    if(ok)*out=value;
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
        if(!online){LOCK();phase_shelly_config_ok=false;phase_shelly_transport_lost_locked(now);UNLOCK();vTaskDelay(ticks(1000));continue;}
        /* Modbus only provides bits, not input mode/inversion/timers. Validate
           the real device over read-only RPC for both switch transports. */
        if(!configured||now>=next_config){
            int validation=phase_shelly_validate(host,password,body,4096);bool valid=validation>0;now=now_ms();
            LOCK();
            if(valid){phase_shelly_config_ok=true;phase_shelly_config_at=now;}
            else if(validation<0){phase_shelly_read_failure_locked(false,now);phase_shelly_config_ok=false;phase_shelly_config_at=0;phase_shelly_lost_locked();}
            else phase_shelly_transport_missing_locked(now);
            UNLOCK();
            /* Do not refresh status while overdue configuration is unknown.
               Repeated failures therefore expire the unchanged 5 s status
               proof; cached configuration cannot be renewed indefinitely. */
            if(!valid){vTaskDelay(ticks(validation==0?500:3000));continue;}
            next_config=now+30000;
        }
        shelly_phase_feedback_t feedback={0};bool read=false,transport_missing=false;
        if(settings.phase_actuator_type==3){
            LOCK();phase_shelly_http_diag_valid=false;phase_shelly_http_resources_valid=false;UNLOCK();
            read=phase_shelly_modbus_read(host,&feedback);
        }
        else{
            int code=phase_shelly_http(host,password,"/rpc/Shelly.GetStatus",body,4096);
            transport_missing=code==0;
            read=code==200&&shelly_phase_feedback(body,&feedback);
        }
        now=now_ms();
        LOCK();
        bool received=false;
        if(transport_missing)phase_shelly_transport_missing_locked(now);
        else received=phase_shelly_receive_locked(&feedback,read,now);
        bool issue=received&&phase_shelly_take_command_locked(now);
        UNLOCK();
        if(issue){
            char path[80];bool on=settings.phase_shelly_true_three_phase?phase_shelly_goal==3:phase_shelly_goal==1;
            bool ok;
            if(settings.phase_actuator_type==3)ok=phase_output_coil(host,0,true,&on);
            else{
                ok=shelly_output_path(path,sizeof(path),"Set",0,on)&&
                    phase_shelly_http(host,password,path,body,4096)==200&&shelly_output_ack(body);
            }
            LOCK();phase_shelly_ack_locked(ok);UNLOCK();
        }
        LOCK();bool switching=phase_state==PHASE_STOPPING||phase_state==PHASE_MOVING||phase_state==PHASE_SETTLING;UNLOCK();
        vTaskDelay(ticks(!read?1500:switching?250:1000));
    }
}
