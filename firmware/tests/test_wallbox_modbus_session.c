#include "ems_core.h"
#include "em24_modbus.h"
#include "wallbox_modbus_session.h"
#include <assert.h>
#include <errno.h>
#include <stdio.h>
#include <string.h>
#include <sys/time.h>
typedef struct {int unused;} fixture_fd_set;
#define fd_set fixture_fd_set
#undef FD_ZERO
#undef FD_SET
#define FD_ZERO(p) ((void)(p))
#define FD_SET(n,p) ((void)(n),(void)(p))
#define select fixture_select
#define MSG_PEEK 1
#define MSG_DONTWAIT 2
typedef struct {bool open,eof,reset;uint8_t request[12],reply[260];size_t request_at,reply_at,reply_size;} peer_t;
static peer_t peers[16];
static int64_t clock_ms;
static unsigned connects,closes,requests,fragment=2,corrupt,connect_cost;
static bool connect_failure,stalled,peer_closes,truncated;
static int64_t now_ms(void){return clock_ms;}
static uint32_t esp_random(void){return 0xabcd;}
static int tcp_connect_timeout(const char *host,uint16_t port,int timeout){
    assert(host&&host[0]&&port==502&&timeout>0);connects++;clock_ms+=connect_cost;
    if(connect_failure)return -1;
    for(unsigned i=0;i<16;i++)if(!peers[i].open){peers[i]=(peer_t){.open=true};return (int)i+10;}
    assert(!"Too many test sockets");return -1;
}
static peer_t *peer(int fd){assert(fd>=10&&fd<26&&peers[fd-10].open);return &peers[fd-10];}
static int close(int fd){peer(fd)->open=false;closes++;return 0;}
static int select(int n,fd_set *r,fd_set *w,fd_set *e,struct timeval *wait){
    (void)n;(void)r;(void)w;(void)e;
    if(stalled){clock_ms+=wait->tv_sec*1000+wait->tv_usec/1000;return 0;}
    clock_ms++;return 1;
}
static void reply(peer_t *p){
    unsigned bytes=(((unsigned)p->request[10]<<8)|p->request[11])*2;
    assert(bytes<=250);uint8_t header[]={p->request[0],p->request[1],0,0,0,(uint8_t)(bytes+3),p->request[6],4,(uint8_t)bytes};
    if(corrupt==1)header[0]^=1;
    if(corrupt==2)header[3]=1;
    if(corrupt==3)header[6]^=1;
    if(corrupt==4)header[7]=0x84;
    if(corrupt==5)header[5]++;
    if(corrupt==6)header[8]--;
    memcpy(p->reply,header,9);for(unsigned i=0;i<bytes;i++)p->reply[9+i]=(uint8_t)(0x30+i);
    p->reply_size=9+bytes;p->reply_at=0;p->eof=peer_closes||truncated;
    if(truncated)p->reply_size-=2;
    if(corrupt==7)p->reply[p->reply_size++]=0xaa;
    requests++;
}
static int send(int fd,const void *data,size_t length,int flags){
    peer_t *p=peer(fd);assert(flags==0);clock_ms++;
    if(p->reset){errno=ECONNRESET;return -1;}
    if(p->request_at==12){assert(p->reply_at==p->reply_size);p->request_at=0;}
    size_t n=length<fragment?length:fragment;assert(p->request_at+n<=12);
    memcpy(p->request+p->request_at,data,n);p->request_at+=n;if(p->request_at==12)reply(p);return (int)n;
}
static int recv(int fd,void *data,size_t length,int flags){
    peer_t *p=peer(fd);
    if(p->reset){errno=ECONNRESET;return -1;}
    if(flags){
        assert(flags==(MSG_PEEK|MSG_DONTWAIT)&&length==1);
        if(p->reply_at<p->reply_size){*(uint8_t *)data=p->reply[p->reply_at];return 1;}
        if(p->eof)return 0;errno=EAGAIN;return -1;
    }
    clock_ms++;
    if(p->reply_at==p->reply_size){if(p->eof)return 0;errno=EAGAIN;return -1;}
    size_t n=length<fragment?length:fragment;if(n>p->reply_size-p->reply_at)n=p->reply_size-p->reply_at;
    memcpy(data,p->reply+p->reply_at,n);p->reply_at+=n;return (int)n;
}
#include "bounded_socket.inc"
#include "meter_tcp.inc"
#include "wallbox_modbus_session.inc"
static void reset(void){memset(peers,0,sizeof(peers));clock_ms=0;connects=closes=requests=corrupt=connect_cost=0;fragment=2;connect_failure=stalled=peer_closes=truncated=false;}
static bool read_sample(wallbox_modbus_session_t *s,uint8_t *data){return wallbox_modbus_session_read(s,"192.0.2.1",1,7,1011,6,data,12,1000);}
static void assert_zero(const uint8_t *data){for(unsigned i=0;i<12;i++)assert(!data[i]);}
int main(void){
    (void)read_em24_modbus;
    uint8_t data[12];reset();wallbox_modbus_session_t a={.sock=-1};
    assert(read_sample(&a,data));int fd=a.sock;uint16_t previous=a.transaction;
    for(unsigned i=0;i<4;i++){assert(read_sample(&a,data)&&a.sock==fd&&a.transaction!=previous);previous=a.transaction;}
    assert(connects==1&&!closes&&requests==5);for(unsigned i=0;i<12;i++)assert(data[i]==0x30+i);
    /* Independent worker contexts cannot share mutable socket or TX state. */
    wallbox_modbus_session_t b={.sock=-1};assert(read_sample(&b,data)&&b.sock!=a.sock&&connects==2);
    uint16_t b_tx=b.transaction;assert(read_sample(&a,data)&&b.transaction==b_tx);wallbox_modbus_session_close(&a);assert(b.sock>=0&&read_sample(&b,data));wallbox_modbus_session_close(&b);
    reset();a=(wallbox_modbus_session_t){.sock=-1};assert(read_sample(&a,data));
    assert(wallbox_modbus_session_read(&a,"192.0.2.2",1,7,1011,6,data,12,1000)&&connects==2&&closes==1);
    assert(wallbox_modbus_session_read(&a,"192.0.2.2",2,7,1011,6,data,12,1000)&&connects==3&&closes==2);
    assert(wallbox_modbus_session_read(&a,"192.0.2.2",2,8,1011,6,data,12,1000)&&connects==4&&closes==3);wallbox_modbus_session_close(&a);
    for(unsigned error=1;error<=7;error++){
        reset();a=(wallbox_modbus_session_t){.sock=-1};corrupt=error;memset(data,0xaa,sizeof(data));
        assert(!read_sample(&a,data)&&a.sock<0&&connects==1&&requests==1&&closes==1);assert_zero(data);
        corrupt=0;assert(read_sample(&a,data)&&connects==2&&requests==2);wallbox_modbus_session_close(&a);
    }
    reset();a=(wallbox_modbus_session_t){.sock=-1};truncated=true;assert(!read_sample(&a,data)&&a.sock<0&&requests==1);assert_zero(data);
    reset();a=(wallbox_modbus_session_t){.sock=-1};stalled=true;assert(!read_sample(&a,data)&&clock_ms==1000&&connects==1&&requests==0&&closes==1);assert_zero(data);
    reset();a=(wallbox_modbus_session_t){.sock=-1};connect_cost=1000;assert(!read_sample(&a,data)&&clock_ms==1000&&requests==0&&closes==1);
    reset();a=(wallbox_modbus_session_t){.sock=-1};connect_failure=true;assert(!read_sample(&a,data)&&connects==1&&!requests&&!closes);assert_zero(data);
    reset();a=(wallbox_modbus_session_t){.sock=-1};peer_closes=true;assert(read_sample(&a,data)&&a.sock<0&&closes==1);assert(read_sample(&a,data)&&connects==2&&closes==2);
    /* A reboot/reset or unsolicited old data between polls fails this poll;
       no same-call reconnect/replay or stale response acceptance. */
    for(unsigned error=0;error<3;error++){
        reset();a=(wallbox_modbus_session_t){.sock=-1};assert(read_sample(&a,data));
        peer_t *p=peer(a.sock);if(error==0)p->reset=true;else if(error==1)p->eof=true;else p->reply[p->reply_size++]=0x99;
        assert(!read_sample(&a,data)&&connects==1&&requests==1&&closes==1&&a.sock<0);assert_zero(data);
        assert(read_sample(&a,data)&&connects==2&&requests==2);wallbox_modbus_session_close(&a);
    }
    reset();a=(wallbox_modbus_session_t){.sock=-1,.transaction=UINT16_MAX,.transaction_seeded=true};assert(read_sample(&a,data)&&a.transaction==1);wallbox_modbus_session_close(&a);
    /* Existing generic wrapper still opens/closes for every scan/house call. */
    reset();assert(modbus_tcp_read_input("192.0.2.3",1011,6,data,12,1000));assert(modbus_tcp_read_input("192.0.2.3",1011,6,data,12,1000));assert(connects==2&&closes==2&&requests==2);
    puts("PASS: actual persistent Modbus framing over fragmented I/O; one connection across polls, isolated owners, changing host/unit/revision, fresh TX IDs, one total deadline, truncated/wrong/unsolicited/reset responses discarded without replay, peer EOF and generic ephemeral behavior preserved");
}
