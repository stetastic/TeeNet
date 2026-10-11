#include "ems_core.h"
#include <assert.h>
#include <errno.h>
#include <stdio.h>
#include <string.h>
#include <sys/time.h>
#define select test_select
typedef int uart_port_t;
#define WALLBOX_UART 1
#define XEMEX_UART 2
#define HOUSE_UART 0
#define LOCK() ((void)0)
#define UNLOCK() ((void)0)
#define ticks(x) (x)
#define ESP_OK 0
#define F_GETFL 1
#define F_SETFL 2
#define O_NONBLOCK 4
#define IPPROTO_TCP 6
#define TCP_NODELAY 1
#define MSG_DONTWAIT 2
typedef struct {int unused;} test_fd_set;
#define fd_set test_fd_set
#undef FD_ZERO
#undef FD_SET
#define FD_ZERO(p) ((void)(p))
#define FD_SET(n,p) ((void)(n),(void)(p))
static settings_t settings;
static bool wifi_online=true,restarting,ota_in_progress,wallbox_ready,meter_ready,house_bus_ready;
static int64_t elapsed;
static int connect_calls,connect_result=10,closed,read_size=1,read_ready=1,uart_reads,uart_writes;
static int read_error,select_error,flush_interrupts;
static uint8_t incoming[32],outgoing[32];static size_t rx_pos,tx_pos;
static int64_t now_ms(void){return elapsed;}
static int tcp_connect_timeout(const char *host,unsigned port,int timeout){assert(host[0]);assert(port==8899&&timeout==300);connect_calls++;return connect_result;}
static int fcntl(int sock,int operation,int flags){(void)sock;(void)operation;(void)flags;return 0;}
static int setsockopt(int s,int p,int k,const void *v,size_t n){(void)s;(void)p;(void)k;(void)v;(void)n;return 0;}
static int close(int sock){(void)sock;closed++;return 0;}
static int select(int n,fd_set *r,fd_set *w,fd_set *e,struct timeval *wait){(void)n;(void)r;(void)w;(void)e;elapsed+=wait->tv_sec*1000+wait->tv_usec/1000;if(select_error){errno=select_error;return -1;}return read_ready;}
static int recv(int sock,void *buffer,size_t length,int flags){
 (void)sock;if(flags==MSG_DONTWAIT){if(flush_interrupts>0){flush_interrupts--;errno=EINTR;}else errno=EAGAIN;return -1;}
 if(read_error){errno=read_error;return -1;}
 if(read_size<=0)return read_size;
 size_t count=length<(size_t)read_size?length:(size_t)read_size;
 memcpy(buffer,incoming+rx_pos,count);rx_pos+=count;return (int)count;
}
static bool socket_exact(int sock,uint8_t *data,size_t length,bool sending,int64_t deadline){
 (void)sock;assert(sending&&deadline>elapsed);memcpy(outgoing+tx_pos,data,length);tx_pos+=length;return true;
}
static int uart_read_bytes(int port,void *data,size_t n,int timeout){(void)port;(void)data;(void)n;(void)timeout;uart_reads++;return 0;}
static int uart_flush_input(int port){(void)port;return ESP_OK;}
static int uart_write_bytes(int port,const void *data,size_t n){(void)port;(void)data;uart_writes++;return (int)n;}
static int uart_wait_tx_done(int port,int timeout){(void)port;(void)timeout;return ESP_OK;}
#include "rs485_transport.inc"
int main(void){
 settings_defaults(&settings);uint8_t bytes[17]={1,3,0x50,0xc,0,6,0x94,0xcf},received[17];
 assert(rs485_read(WALLBOX_UART,received,1,20)==0&&uart_reads==1&&connect_calls==0);
 assert(rs485_write(WALLBOX_UART,bytes,8,100)&&uart_writes==1);
 settings.shell_rs485_interface=1;strcpy(settings.shell_rs485_host,"192.168.1.20:8899");memcpy(incoming,bytes,8);
 assert(rs485_flush(WALLBOX_UART));assert(wallbox_ready&&connect_calls==1);
 for(unsigned i=0;i<8;i++)assert(rs485_read(WALLBOX_UART,received+i,1,20)==1);
 assert(!memcmp(received,bytes,8));assert(connect_calls==1); /* Keep one persistent socket. */
 assert(rs485_write(WALLBOX_UART,bytes,8,100));assert(!memcmp(outgoing,bytes,8)); /* No MBAP or CRC rewriting. */
 select_error=EINTR;assert(rs485_read(WALLBOX_UART,received,1,20)==0&&wallbox_ready&&closed==0);select_error=0;
 read_error=EINTR;assert(rs485_read(WALLBOX_UART,received,1,20)==0&&wallbox_ready&&closed==0);read_error=EAGAIN;
 assert(rs485_read(WALLBOX_UART,received,1,20)==0&&wallbox_ready&&closed==0);read_error=0;
 flush_interrupts=1;assert(rs485_flush(WALLBOX_UART)&&wallbox_ready&&closed==0&&flush_interrupts==0);
 read_ready=0;assert(rs485_read(WALLBOX_UART,received,1,20)==0&&wallbox_ready);
 read_ready=1;read_size=0;assert(rs485_read(WALLBOX_UART,received,1,20)==-1&&!wallbox_ready&&closed==1);
 int attempts=connect_calls;assert(rs485_read(WALLBOX_UART,received,1,20)==-1&&connect_calls==attempts);
 elapsed+=500;read_size=1;rx_pos=0;assert(rs485_read(WALLBOX_UART,received,1,20)==1&&wallbox_ready);
 settings.meter_rs485_interface=1;strcpy(settings.meter_rs485_host,"192.168.1.20:8900");connect_result=-1;
 assert(rs485_read(XEMEX_UART,received,1,20)==-1&&!meter_ready&&wallbox_ready);
 attempts=connect_calls;assert(rs485_read(XEMEX_UART,received,1,20)==-1&&connect_calls==attempts);
 elapsed+=500;assert(rs485_read(XEMEX_UART,received,1,20)==-1);
 attempts=connect_calls;elapsed+=500;assert(rs485_read(XEMEX_UART,received,1,20)==-1&&connect_calls==attempts);
 wifi_online=false;assert(rs485_read(WALLBOX_UART,received,1,20)==-1&&!wallbox_ready);
 assert(closed==2);
 puts("PASS: UART unchanged, raw RTU bytes, fragmentation, persistent TCP, idle timeout, disconnect and independent reconnect backoff");
}
