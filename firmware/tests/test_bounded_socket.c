#include <assert.h>
#include <errno.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <sys/time.h>
typedef struct {int unused;} test_fd_set;
#define fd_set test_fd_set
#undef FD_ZERO
#undef FD_SET
#define FD_ZERO(p) ((void)(p))
#define FD_SET(n,p) ((void)(n),(void)(p))
#define select test_select
static int64_t clock_ms;
static int select_errors,io_errors,io_errno=EAGAIN,ready=1,fragment=2,closed_after=-1,calls;
static size_t transferred;
static uint8_t source[]={1,3,0x50,0x0c,0,6,0x94,0xcf},written[sizeof(source)];
static int64_t now_ms(void){return clock_ms;}
static int select(int n,fd_set *r,fd_set *w,fd_set *e,struct timeval *wait){
    (void)n;(void)r;(void)w;(void)e;
    if(select_errors){select_errors--;clock_ms++;errno=EINTR;return -1;}
    if(!ready){clock_ms+=wait->tv_sec*1000+wait->tv_usec/1000;return 0;}
    clock_ms++;return ready;
}
static int transfer(void *buffer,size_t length,bool sending){
    calls++;clock_ms++;
    if(io_errors){io_errors--;errno=io_errno;return -1;}
    if(closed_after>=0 && transferred>=(size_t)closed_after)return 0;
    size_t count=length<(size_t)fragment?length:(size_t)fragment;
    if(sending)memcpy(written+transferred,buffer,count);
    else memcpy(buffer,source+transferred,count);
    transferred+=count;return (int)count;
}
static int send(int sock,const void *data,size_t length,int flags){(void)sock;(void)flags;return transfer((void *)data,length,true);}
static int recv(int sock,void *data,size_t length,int flags){(void)sock;(void)flags;return transfer(data,length,false);}
#include "bounded_socket.inc"
static void reset(void){clock_ms=0;select_errors=io_errors=calls=0;ready=1;fragment=2;closed_after=-1;transferred=0;memset(written,0,sizeof(written));}
int main(void){
    uint8_t output[sizeof(source)]={0};reset();
    select_errors=2;io_errors=2;io_errno=EAGAIN;
    assert(socket_exact(10,output,sizeof(output),false,100));assert(!memcmp(source,output,sizeof(source)));
    reset();io_errors=1;io_errno=EINTR;
    assert(socket_exact(10,source,sizeof(source),true,100));assert(!memcmp(source,written,sizeof(source)));
    reset();io_errors=1;io_errno=EWOULDBLOCK;
    assert(socket_exact(10,output,sizeof(output),false,100));
    reset();io_errors=1000;io_errno=EAGAIN;
    assert(!socket_exact(10,output,sizeof(output),false,20));assert(clock_ms==20&&calls==10&&transferred==0);
    reset();ready=0;assert(!socket_exact(10,output,sizeof(output),false,20));assert(clock_ms==20&&calls==0);
    reset();closed_after=4;assert(!socket_exact(10,output,sizeof(output),false,100));assert(transferred==4);
    reset();io_errors=1;io_errno=ECONNRESET;assert(!socket_exact(10,output,sizeof(output),false,100));assert(calls==1);
    reset();assert(!socket_exact(10,output,sizeof(output),false,0));assert(calls==0);
    puts("PASS: exact TCP bytes, fragmented reads/writes, transient errors, EOF, fatal errors and one bounded deadline");
}
