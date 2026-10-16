/* Native Shelly coil or unauthenticated RPC. Only the expert phase worker calls writes. */
static bool phase_output_coil(const char *host,unsigned id,bool writing,bool *on){
    if(!shelly_phase_endpoint_valid(host,64)||(id!=0&&id!=100)||!on)return false;
    int64_t deadline=now_ms()+800;int sock=tcp_connect_timeout(host,502,800);if(sock<0)return false;
    uint8_t request[12],reply[12];shelly_output_coil_request(request,(uint16_t)esp_random(),id,writing,*on);
    size_t size=writing?12:10;
    bool ok=socket_exact(sock,request,12,true,deadline)&&socket_exact(sock,reply,size,false,deadline)&&
        shelly_output_coil_reply(reply,size,request,on);
    close(sock);return ok;
}
static bool phase_output_rpc(const char *host,unsigned id,bool writing,bool *on,const char *password){
    char path[96];char *body=malloc(1024);if(!body)return false;bool ok=false;
    if(writing){
        ok=shelly_output_path(path,sizeof(path),"Set",id,*on)&&
            phase_shelly_http(host,password,path,body,1024)==200&&shelly_output_ack(body);
    }else{
        ok=shelly_output_path(path,sizeof(path),"GetConfig",id,false)&&
            phase_shelly_http(host,password,path,body,1024)==200&&shelly_output_config(body,id)&&
            shelly_output_path(path,sizeof(path),"GetStatus",id,false)&&
            phase_shelly_http(host,password,path,body,1024)==200&&shelly_output_status(body,id,on);
    }
    free(body);return ok;
}
static bool phase_output_direct_read(uint8_t transport,const char *host,unsigned id,bool *on){
    return transport==1?phase_output_coil(host,id,false,on):transport==2&&phase_output_rpc(host,id,false,on,"");
}
static bool phase_output_direct_write(uint8_t transport,const char *host,unsigned id,bool on){
    return transport==1?phase_output_coil(host,id,true,&on):transport==2&&phase_output_rpc(host,id,true,&on,"");
}
static bool phase_output_direct_read_auth(uint8_t transport,const char *host,unsigned id,const char *password,bool *on){
    return transport==3?(password[0]&&phase_output_rpc(host,id,false,on,password)):
        phase_output_direct_read(transport,host,id,on);
}
static bool phase_output_direct_write_auth(uint8_t transport,const char *host,unsigned id,const char *password,bool on){
    return transport==3?(password[0]&&phase_output_rpc(host,id,true,&on,password)):
        phase_output_direct_write(transport,host,id,on);
}
static bool phase_output_input(const char *host,unsigned id,bool *closed){
    if(!shelly_phase_endpoint_valid(host,64)||id>1||!closed)return false;
    int64_t deadline=now_ms()+800;int sock=tcp_connect_timeout(host,502,800);if(sock<0)return false;
    uint8_t request[12],reply[10];shelly_output_coil_request(request,(uint16_t)esp_random(),id,false,false);
    request[7]=2;
    bool ok=socket_exact(sock,request,12,true,deadline)&&socket_exact(sock,reply,10,false,deadline)&&
        !memcmp(reply,request,4)&&reply[4]==0&&reply[5]==4&&reply[6]==1&&reply[7]==2&&reply[8]==1&&reply[9]<=1;
    close(sock);if(ok)*closed=reply[9]==1;return ok;
}
