/* Transparent RTU over TCP: CRC and the Shell's master/slave roles are unchanged.
   Each bus owns its socket; network IO never runs inside the HTTP server. */
typedef struct { int sock; unsigned failures; int64_t retry_at; } rs485_channel_t;
static rs485_channel_t rs485_channels[3]={{.sock=-1},{.sock=-1},{.sock=-1}};

static unsigned rs485_index(uart_port_t port) { return port==WALLBOX_UART?0:port==XEMEX_UART?1:2; }
static bool rs485_tcp(uart_port_t port) {
    return port==WALLBOX_UART?settings.shell_rs485_interface==1:
        port==XEMEX_UART?settings.meter_rs485_interface==1:settings.house_rs485_interface==1;
}
static void rs485_ready(uart_port_t port,bool ready) {
    LOCK();if(port==WALLBOX_UART)wallbox_ready=ready;
    else if(port==XEMEX_UART)meter_ready=ready;
    else house_bus_ready=ready;
    UNLOCK();
}
static void rs485_close(uart_port_t port) {
    rs485_channel_t *c=&rs485_channels[rs485_index(port)];
    if(c->sock>=0)close(c->sock);
    c->sock=-1;if(c->failures<4)c->failures++;
    c->retry_at=now_ms()+(500u<<(c->failures-1));
    rs485_ready(port,false);
}
static int rs485_connect(uart_port_t port) {
    rs485_channel_t *c=&rs485_channels[rs485_index(port)];
    LOCK();bool connected=wifi_online&&!restarting&&!ota_in_progress;UNLOCK();
    if(!connected){if(c->sock>=0)rs485_close(port);rs485_ready(port,false);return -1;}
    if(c->sock>=0)return c->sock;
    if(now_ms()<c->retry_at)return -1;
    const char *endpoint=port==WALLBOX_UART?settings.shell_rs485_host:
        port==XEMEX_UART?settings.meter_rs485_host:settings.house_rs485_host;
    c->sock=tcp_connect_timeout(endpoint,8899,300);
    if(c->sock<0){rs485_close(port);return -1;}
    int flags=fcntl(c->sock,F_GETFL,0);
    if(flags<0 || fcntl(c->sock,F_SETFL,flags|O_NONBLOCK)<0){rs485_close(port);return -1;}
    int enabled=1;setsockopt(c->sock,IPPROTO_TCP,TCP_NODELAY,&enabled,sizeof(enabled));
    rs485_ready(port,true);return c->sock;
}
static int rs485_read(uart_port_t port,uint8_t *data,size_t length,int wait_ms) {
    if(!rs485_tcp(port))return uart_read_bytes(port,data,length,ticks(wait_ms));
    int sock=rs485_connect(port);if(sock<0)return -1;
    fd_set reads;FD_ZERO(&reads);FD_SET(sock,&reads);
    struct timeval wait={.tv_sec=wait_ms/1000,.tv_usec=(wait_ms%1000)*1000};
    int result=select(sock+1,&reads,NULL,NULL,&wait);
    if(result==0)return 0;
    if(result<0 && errno==EINTR)return 0;
    if(result<0){rs485_close(port);return -1;}
    int count=recv(sock,data,length,0);
    if(count<0 && (errno==EAGAIN||errno==EWOULDBLOCK||errno==EINTR))return 0;
    if(count<=0){rs485_close(port);return -1;}
    rs485_channels[rs485_index(port)].failures=0;
    return count;
}
static bool rs485_flush(uart_port_t port) {
    if(!rs485_tcp(port))return uart_flush_input(port)==ESP_OK;
    int sock=rs485_connect(port);if(sock<0)return false;
    uint8_t discarded[64];size_t drained=0;unsigned interrupts=0;
    while(drained<512){
        int count=recv(sock,discarded,sizeof(discarded),MSG_DONTWAIT);
        if(count<0 && errno==EINTR && ++interrupts<4)continue;
        if(count<0 && (errno==EAGAIN||errno==EWOULDBLOCK))return true;
        if(count<=0){rs485_close(port);return false;}
        drained+=(size_t)count;
    }
    rs485_close(port);return false; /* Unbounded noise must not stall a task. */
}
static bool rs485_write(uart_port_t port,const uint8_t *data,size_t length,int timeout_ms) {
    if(!rs485_tcp(port))return uart_write_bytes(port,data,length)==(int)length &&
        uart_wait_tx_done(port,ticks(timeout_ms))==ESP_OK;
    int sock=rs485_connect(port);if(sock<0)return false;
    if(!socket_exact(sock,(uint8_t *)data,length,true,now_ms()+timeout_ms)){
        rs485_close(port);return false;
    }
    return true;
}
