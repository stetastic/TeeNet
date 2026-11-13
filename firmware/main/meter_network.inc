/* Bounded HTTP and Modbus TCP transport for external meters. */
static int http_get_body_mode(const char *url,char *data,size_t capacity,int timeout_ms,bool allow_close_json) {
    if(!url||!data||capacity<2)return 0;
    http_body_t body={.data=data,.capacity=capacity};data[0]=0;
    esp_http_client_config_t cfg={
        .url=url,.auth_type=HTTP_AUTH_TYPE_NONE,.timeout_ms=timeout_ms,.disable_auto_redirect=true,
        .max_authorization_retries=0,.event_handler=http_body_event,.user_data=&body,
        .crt_bundle_attach=esp_crt_bundle_attach
    };
    esp_http_client_handle_t client=esp_http_client_init(&cfg);if(!client)return 0;
    esp_err_t err=esp_http_client_perform(client);
    int code=err==ESP_OK?esp_http_client_get_status_code(client):0;
    bool complete=esp_http_client_is_complete_data_received(client);
    /* Shell's local status ends with Connection: close and has neither
       Content-Length nor chunk framing. ESP-IDF 5.3 cannot confirm that form
       with is_complete_data_received. Accept only its bounded, complete JSON
       document; framed or truncated replies keep the strict normal check. */
    if(code==200 && !complete && allow_close_json && !body.framed && body.connection_close && !body.overflow){
        cJSON *json=cJSON_ParseWithOpts(data,NULL,true);
        complete=cJSON_IsObject(json);cJSON_Delete(json);
    }
    if(body.overflow || (code==200 && !complete))code=0;
    esp_http_client_cleanup(client);return code;
}
static int http_get_body(const char *url,char *data,size_t capacity,int timeout_ms){
    return http_get_body_mode(url,data,capacity,timeout_ms,false);
}

static bool split_host_port(const char *source,char *host,size_t capacity,uint16_t *port) {
    if(!source || !source[0] || !host || capacity<2 || !port)return false;
    size_t length=strlen(source);if(length>=capacity)return false;
    strcpy(host,source);*port=502;
    char *separator=strrchr(host,':');
    if(separator){
        char *end=NULL;long value=strtol(separator+1,&end,10);
        if(end==separator+1 || *end || value<1 || value>65535)return false;
        *separator=0;*port=(uint16_t)value;
    }
    return host[0]!=0;
}

static int tcp_connect_timeout(const char *source,uint16_t default_port,int timeout_ms) {
    char host[64];uint16_t port;
    if(!split_host_port(source,host,sizeof(host),&port))return -1;
    if(!strchr(source,':'))port=default_port;
    char service[6];snprintf(service,sizeof(service),"%u",port);
    struct addrinfo hints={.ai_family=AF_INET,.ai_socktype=SOCK_STREAM,.ai_protocol=IPPROTO_TCP},*addresses=NULL;
    if(getaddrinfo(host,service,&hints,&addresses)!=0)return -1;
    int connected=-1;
    for(struct addrinfo *entry=addresses;entry && connected<0;entry=entry->ai_next){
        int sock=socket(entry->ai_family,entry->ai_socktype,entry->ai_protocol);if(sock<0)continue;
        int flags=fcntl(sock,F_GETFL,0);
        if(flags<0 || fcntl(sock,F_SETFL,flags|O_NONBLOCK)<0){close(sock);continue;}
        int result=connect(sock,entry->ai_addr,entry->ai_addrlen);
        if(result<0 && errno!=EINPROGRESS && errno!=EAGAIN){close(sock);continue;}
        if(result<0){
            fd_set writes;FD_ZERO(&writes);FD_SET(sock,&writes);
            struct timeval wait={.tv_sec=timeout_ms/1000,.tv_usec=(timeout_ms%1000)*1000};
            if(select(sock+1,NULL,&writes,NULL,&wait)<=0){close(sock);continue;}
            int error=0;socklen_t size=sizeof(error);
            if(getsockopt(sock,SOL_SOCKET,SO_ERROR,&error,&size)<0 || error){close(sock);continue;}
        }
        if(fcntl(sock,F_SETFL,flags)<0){close(sock);continue;}
        struct timeval io={.tv_sec=timeout_ms/1000,.tv_usec=(timeout_ms%1000)*1000};
        setsockopt(sock,SOL_SOCKET,SO_RCVTIMEO,&io,sizeof(io));setsockopt(sock,SOL_SOCKET,SO_SNDTIMEO,&io,sizeof(io));
        connected=sock;
    }
    freeaddrinfo(addresses);return connected;
}

#include "bounded_socket.inc"

#include "meter_tcp.inc"
#include "wallbox_modbus_session.inc"

static bool read_shelly_modbus(const char *host,const char *type,shelly_modbus_reading_t *reading,int timeout_ms) {
    if(!strcmp(type,"shelly_gen2")){
        uint8_t data[SHELLY_EM_COUNT*2];
        return modbus_tcp_read_input(host,SHELLY_EM_START,SHELLY_EM_COUNT,data,sizeof(data),timeout_ms) &&
            shelly_modbus_decode_em(data,sizeof(data),reading);
    }
    if(!strcmp(type,"shelly_em1")){
        uint8_t data[SHELLY_EM1_COUNT*2];
        return modbus_tcp_read_input(host,SHELLY_EM1_START,SHELLY_EM1_COUNT,data,sizeof(data),timeout_ms) &&
            shelly_modbus_decode_em1(data,sizeof(data),reading);
    }
    return false;
}

