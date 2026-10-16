#include "shelly_phase.h"
#include <assert.h>
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
static int64_t clock_ms=1000;static unsigned closes,writes,http_calls;
static uint8_t sent[12];static bool output,corrupt,fail,unsafe;static int http_code=200;static const char *expected_password="";
static int64_t now_ms(void){return clock_ms;}
static uint32_t esp_random(void){return 0x1234;}
static int tcp_connect_timeout(const char *host,uint16_t port,int timeout){assert(!strcmp(host,"192.168.1.88")&&port==502&&timeout==800);return fail?-1:3;}
static void close(int sock){assert(sock==3);closes++;}
static bool socket_exact(int sock,uint8_t *data,size_t count,bool writing,int64_t deadline){
    assert(sock==3&&deadline==1800);
    if(writing){assert(count==12);memcpy(sent,data,12);if(sent[7]==5){writes++;output=sent[10]==255;}return true;}
    memcpy(data,sent,8);
    if(sent[7]==5){assert(count==12);memcpy(data,sent,12);}
    else{assert((sent[7]==1||sent[7]==2)&&count==10);data[5]=4;data[7]=sent[7];data[8]=1;data[9]=output?1:0;}
    if(corrupt)data[1]^=1;return true;
}
static int phase_shelly_http(const char *host,const char *password,const char *path,char *body,size_t capacity){
    assert(!strcmp(host,"192.168.1.88")&&!strcmp(password,expected_password)&&capacity==1024);http_calls++;
    if(http_code!=200){strcpy(body,"{}");return http_code;}
    if(strstr(path,"GetConfig"))snprintf(body,capacity,"{\"id\":0,\"in_mode\":\"%s\",\"initial_state\":\"off\",\"auto_on\":false,\"auto_off\":false}",unsafe?"follow":"detached");
    else if(strstr(path,"GetStatus"))snprintf(body,capacity,"{\"id\":0,\"output\":%s}",output?"true":"false");
    else{assert(strstr(path,"Switch.Set?id=0&on=")&&!strstr(path,"toggle"));snprintf(body,capacity,"{\"was_on\":%s}",output?"true":"false");output=strstr(path,"on=true")!=NULL;writes++;}
    return 200;
}
#include "phase_output_network.inc"
int main(void){
    char host[64]="192.168.1.88";bool on=false;
    assert(phase_output_direct_read(1,host,0,&on)&&!on&&sent[8]==0&&sent[9]==100);
    assert(phase_output_direct_write(1,host,0,true)&&output&&sent[7]==5&&sent[10]==255&&sent[11]==0);
    assert(phase_output_direct_read(1,host,0,&on)&&on);
    assert(phase_output_direct_write(1,host,100,false)&&!output&&sent[8]==4&&sent[9]==76);
    corrupt=true;assert(!phase_output_direct_write(1,host,0,true));corrupt=false;
    fail=true;assert(!phase_output_direct_read(1,host,0,&on));fail=false;
    assert(!phase_output_direct_write(1,host,1,true));
    assert(phase_output_direct_read(2,host,0,&on));
    assert(phase_output_direct_write(2,host,0,false)&&!output);
    assert(phase_output_direct_write(2,host,0,true)&&output);
    unsigned count=http_calls;http_code=401;assert(!phase_output_direct_read(2,host,0,&on)&&http_calls==count+1);http_code=200;
    unsafe=true;count=http_calls;assert(!phase_output_direct_read(2,host,0,&on)&&http_calls==count+1);unsafe=false;
    assert(writes==5&&closes==5);
    assert(phase_output_input(host,0,&on)&&sent[7]==2&&sent[9]==100);
    assert(phase_output_input(host,1,&on)&&sent[7]==2&&sent[9]==110);
    assert(!phase_output_input(host,2,&on));
    unsigned calls=http_calls;assert(!phase_output_direct_read_auth(3,host,0,"",&on)&&http_calls==calls);
    expected_password="synthetic-secret";
    assert(phase_output_direct_read_auth(3,host,0,expected_password,&on));
    assert(phase_output_direct_write_auth(3,host,0,expected_password,false));
    http_code=401;calls=http_calls;assert(!phase_output_direct_read_auth(3,host,0,expected_password,&on)&&http_calls==calls+1);
    puts("PASS: real Shelly adapter: FC01/FC05, 502/unit1, coils100/1100, strict response identity, one write/no retry, RPC absolute states/empty password, 401 and unsafe config blocked");
}
