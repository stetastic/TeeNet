/* Read-only Modbus TCP requests; caller provides bounded socket transport. */
static bool modbus_tcp_read_frame(int sock,uint8_t unit,uint16_t start,uint16_t count,uint8_t *data,uint16_t transaction,int64_t deadline){
    uint8_t request[12]={transaction>>8,transaction&255,0,0,0,6,unit,4,start>>8,start&255,count>>8,count&255};
    uint8_t header[9];bool ok=socket_exact(sock,request,sizeof(request),true,deadline) && socket_exact(sock,header,sizeof(header),false,deadline);
    size_t bytes=(size_t)count*2;
    ok=ok && header[0]==request[0] && header[1]==request[1] && header[2]==0 && header[3]==0 &&
        header[4]==0 && header[5]==bytes+3 && header[6]==unit && header[7]==4 && header[8]==bytes &&
        socket_exact(sock,data,bytes,false,deadline);
    return ok;
}
static bool modbus_tcp_read_input_unit(const char *host,uint8_t unit,uint16_t start,uint16_t count,uint8_t *data,size_t capacity,int timeout_ms) {
    if(!host || !data || !unit || unit>247 || !count || count>125 || capacity<(size_t)count*2)return false;
    int64_t deadline=now_ms()+timeout_ms;
    int sock=tcp_connect_timeout(host,502,timeout_ms);if(sock<0)return false;
    bool ok=modbus_tcp_read_frame(sock,unit,start,count,data,(uint16_t)esp_random(),deadline);
    close(sock);return ok;
}
static bool modbus_tcp_read_input(const char *host,uint16_t start,uint16_t count,uint8_t *data,size_t capacity,int timeout_ms) {
    return modbus_tcp_read_input_unit(host,1,start,count,data,capacity,timeout_ms);
}

static bool read_em24_modbus(const char *host,uint8_t unit,em24_reading_t *reading,int timeout_ms) {
    uint8_t currents[EM24_CURRENT_COUNT*2],power[EM24_POWER_COUNT*2];
    int64_t deadline=now_ms()+timeout_ms;
    if(!modbus_tcp_read_input_unit(host,unit,EM24_CURRENT_START,EM24_CURRENT_COUNT,
                                  currents,sizeof(currents),timeout_ms))return false;
    int remaining=(int)(deadline-now_ms());
    return remaining>0 && modbus_tcp_read_input_unit(host,unit,EM24_POWER_START,EM24_POWER_COUNT,
        power,sizeof(power),remaining) && em24_modbus_decode(currents,sizeof(currents),power,sizeof(power),reading);
}

