#include <assert.h>
#include <stdbool.h>
#include <stdint.h>
#include <stddef.h>
#include <stdio.h>
#include <string.h>
typedef int portMUX_TYPE;
#define portMUX_INITIALIZER_UNLOCKED 0
static unsigned critical_depth,app_locks,new_calls,post_calls;
static bool in_tcpip,fail_new,fail_post,dispatch_immediately;
static int64_t clock_ms;
static void enter_critical(portMUX_TYPE *mux){assert(!app_locks&&*mux==0&&critical_depth++==0);}
static void exit_critical(portMUX_TYPE *mux){assert(*mux==0&&--critical_depth==0);}
#define portENTER_CRITICAL(mux) enter_critical(mux)
#define portEXIT_CRITICAL(mux) exit_critical(mux)
static int64_t now_ms(void){return clock_ms;}
struct tcp_pcb {struct tcp_pcb *next;uint16_t local_port,remote_port;};
struct tcp_pcb_listen {struct tcp_pcb_listen *next;uint16_t local_port;};
static struct tcp_pcb *tcp_active_pcbs,*tcp_tw_pcbs,*tcp_bound_pcbs;
static union {struct tcp_pcb_listen *listen_pcbs;} tcp_listen_pcbs;
struct tcpip_callback_msg {void (*function)(void *);void *arg;};
static struct tcpip_callback_msg message,*queued;
#define ERR_OK 0
static struct tcpip_callback_msg *tcpip_callbackmsg_new(void (*function)(void *),void *arg){
    assert(!app_locks&&!critical_depth&&!in_tcpip);new_calls++;
    if(fail_new)return NULL;message=(struct tcpip_callback_msg){function,arg};return &message;
}
static void dispatch(void){
    assert(queued&&!app_locks&&!critical_depth&&!in_tcpip);
    struct tcpip_callback_msg *next=queued;queued=NULL;in_tcpip=true;
    next->function(next->arg);in_tcpip=false;assert(!critical_depth);
}
static int tcpip_callbackmsg_trycallback(struct tcpip_callback_msg *msg){
    assert(!app_locks&&!critical_depth&&!in_tcpip&&msg==&message&&!queued);post_calls++;
    if(fail_post)return -1;queued=msg;if(dispatch_immediately)dispatch();return ERR_OK;
}
#include "tcp_pcb_diagnostic.inc"
static void reset(void){
    assert(!critical_depth&&!app_locks);
    new_calls=post_calls=0;in_tcpip=fail_new=fail_post=dispatch_immediately=false;
    tcp_active_pcbs=tcp_tw_pcbs=tcp_bound_pcbs=NULL;tcp_listen_pcbs.listen_pcbs=NULL;
    tcp_pcb_diag_message=NULL;tcp_pcb_diag_started=false;tcp_pcb_diag_next=0;queued=NULL;
    memset(&tcp_pcb_diag_cache,0,sizeof(tcp_pcb_diag_cache));clock_ms=1000;
}
static void test_counts(void){
    reset();struct tcp_pcb active[2]={{0}},tw[5]={{0}},bound={0};struct tcp_pcb_listen listener={0};
    active[0].next=&active[1];tcp_active_pcbs=active;tcp_tw_pcbs=tw;tcp_bound_pcbs=&bound;tcp_listen_pcbs.listen_pcbs=&listener;
    for(unsigned i=0;i<4;i++)tw[i].next=&tw[i+1];
    tw[0].local_port=80;tw[0].remote_port=80; /* Incoming takes precedence. */
    tw[1].remote_port=502;tw[2].remote_port=80;tw[3].remote_port=12800;tw[4].remote_port=1883;
    struct tcp_pcb before[5];memcpy(before,tw,sizeof(tw));
    tcp_pcb_diag_start();tcp_pcb_diag_start();assert(new_calls==1);
    tcp_pcb_diag_snapshot_t old=tcp_pcb_diag_read();assert(old.available&&!old.samples&&!old.pending);
    tcp_pcb_diag_schedule(clock_ms);assert(post_calls==1&&tcp_pcb_diag_read().pending&&!tcp_pcb_diag_read().samples);
    tcp_pcb_diag_schedule(clock_ms+1);assert(post_calls==1);
    dispatch();tcp_pcb_diag_snapshot_t value=tcp_pcb_diag_read();
    assert(value.available&&!value.pending&&!value.truncated&&value.samples==1&&value.at==clock_ms);
    assert(value.active==2&&value.time_wait==5&&value.bound==1&&value.listening==1);
    assert(value.tw_incoming_http==1&&value.tw_modbus==1&&value.tw_remote_http==1&&value.tw_shell==1&&value.tw_other==1);
    assert(value.pcb_bytes==8*sizeof(struct tcp_pcb)+sizeof(struct tcp_pcb_listen)&&value.pcb_size==sizeof(struct tcp_pcb));
    assert(!memcmp(before,tw,sizeof(tw))&&new_calls==1&&post_calls==1);
    for(unsigned i=0;i<100;i++){tcp_pcb_diag_snapshot_t read=tcp_pcb_diag_read();assert(read.samples==1);}
    assert(new_calls==1&&post_calls==1); /* Cached GET cannot enqueue or allocate. */
    tcp_pcb_diag_schedule(clock_ms+4999);assert(post_calls==1);
    clock_ms+=5000;tcp_pcb_diag_schedule(clock_ms);assert(post_calls==2);dispatch();assert(tcp_pcb_diag_read().samples==2&&new_calls==1);
}
static void test_failure_and_pending(void){
    reset();fail_new=true;tcp_pcb_diag_start();tcp_pcb_diag_start();tcp_pcb_diag_schedule(1000);tcp_pcb_diag_schedule(100000);
    assert(new_calls==1&&!post_calls&&!tcp_pcb_diag_read().available);
    reset();tcp_pcb_diag_start();fail_post=true;tcp_pcb_diag_schedule(1000);
    assert(post_calls==1&&tcp_pcb_diag_read().queue_skips==1&&!tcp_pcb_diag_read().pending&&!queued);
    fail_post=false;tcp_pcb_diag_schedule(5999);assert(post_calls==1);
    tcp_pcb_diag_schedule(6000);assert(post_calls==2&&tcp_pcb_diag_read().pending);
    tcp_pcb_diag_schedule(11000);tcp_pcb_diag_schedule(16000);assert(post_calls==2); /* Never post same message while pending. */
    clock_ms=16001;dispatch();assert(tcp_pcb_diag_read().queue_skips==1&&tcp_pcb_diag_read().samples==1);
    dispatch_immediately=true;tcp_pcb_diag_schedule(21000);assert(post_calls==3&&!queued&&!tcp_pcb_diag_read().pending&&tcp_pcb_diag_read().samples==2&&new_calls==1);
}
static void test_bounded_walk(void){
    reset();struct tcp_pcb chain[1026];memset(chain,0,sizeof(chain));
    for(unsigned i=0;i<1025;i++)chain[i].next=&chain[i+1];
    tcp_tw_pcbs=chain;tcp_pcb_diag_start();tcp_pcb_diag_schedule(1000);dispatch();
    tcp_pcb_diag_snapshot_t value=tcp_pcb_diag_read();assert(value.truncated&&value.time_wait==1024&&value.tw_other==1024);
    /* Bound is shared across lists, including a corrupt cyclic fixture. */
    tcp_active_pcbs=chain;chain[0].next=&chain[0];tcp_pcb_diag_schedule(6000);dispatch();value=tcp_pcb_diag_read();
    assert(value.truncated&&value.active==1024&&value.time_wait==0);
}
int main(void){
    test_counts();test_failure_and_pending();test_bounded_walk();
    puts("PASS: private TCP PCB census counts/classifies bounded lists without mutation; one startup allocation, 5 s nonblocking scheduling, queue-full/pending/immediate-callback safety, cached-only reads and no application-lock use");
}
