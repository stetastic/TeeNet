#include "wallbox_modbus_session.h"
static inline void wallbox_modbus_session_close(wallbox_modbus_session_t *session){
    if(!session)return;
    if(session->sock>=0)close(session->sock);
    session->sock=-1;session->host[0]=0;session->unit=0;session->revision=0;
}
/* No bytes should be pending between our strictly sequential requests. Peek
   without blocking; never consume an old/delayed reply as a new observation. */
static int wallbox_modbus_session_peek(int sock){
    uint8_t byte;int count=recv(sock,&byte,1,MSG_PEEK|MSG_DONTWAIT);
    if(count<0&&(errno==EAGAIN||errno==EWOULDBLOCK))return 1;
    if(count==0)return 0;
    return -1;
}
static inline bool wallbox_modbus_session_read(wallbox_modbus_session_t *session,const char *host,uint8_t unit,uint32_t revision,uint16_t start,uint16_t count,uint8_t *data,size_t capacity,int timeout_ms){
    size_t bytes=(size_t)count*2;
    if(!session||!host||!host[0]||strlen(host)>=sizeof(session->host)||!data||!unit||unit>247||!count||count>125||capacity<bytes||timeout_ms<=0){
        wallbox_modbus_session_close(session);return false;
    }
    int64_t deadline=now_ms()+timeout_ms;
    if(session->sock>=0&&(session->revision!=revision||session->unit!=unit||strcmp(session->host,host)))wallbox_modbus_session_close(session);
    if(session->sock>=0&&wallbox_modbus_session_peek(session->sock)!=1)goto failed;
    if(session->sock<0){
        session->sock=tcp_connect_timeout(host,502,timeout_ms);
        if(session->sock<0)goto failed;
        strcpy(session->host,host);session->unit=unit;session->revision=revision;
    }
    if(!session->transaction_seeded){session->transaction=(uint16_t)esp_random();session->transaction_seeded=true;}
    if(++session->transaction==0)session->transaction=1;
    if(!modbus_tcp_read_frame(session->sock,unit,start,count,data,session->transaction,deadline))goto failed;
    int remaining=wallbox_modbus_session_peek(session->sock);
    if(remaining<0)goto failed;
    /* A complete valid frame followed by orderly peer close is still a valid
       sample. Reconnect only on the next scheduled poll, never replay here. */
    if(remaining==0)wallbox_modbus_session_close(session);
    return true;
failed:
    memset(data,0,bytes);wallbox_modbus_session_close(session);return false;
}
