/* Private measurement build only. The pinned IDF has no public PCB census;
   read its lists only from the TCP/IP task, never from an HTTP/control task. */
#ifdef ESP_PLATFORM
#include "lwip/tcpip.h"
#include "lwip/priv/tcp_priv.h"
#endif
typedef struct {
    uint32_t active,time_wait,bound,listening,pcb_bytes,pcb_size,samples,queue_skips;
    uint32_t tw_incoming_http,tw_remote_http,tw_modbus,tw_shell,tw_other;
    int64_t at;
    bool available,pending,truncated;
} tcp_pcb_diag_snapshot_t;
static portMUX_TYPE tcp_pcb_diag_mux=portMUX_INITIALIZER_UNLOCKED;
static tcp_pcb_diag_snapshot_t tcp_pcb_diag_cache;
static struct tcpip_callback_msg *tcp_pcb_diag_message;
static bool tcp_pcb_diag_started;
static int64_t tcp_pcb_diag_next;
#define TCP_PCB_DIAG_LIMIT 1024u
static void tcp_pcb_diag_collect(void *arg){
    (void)arg;tcp_pcb_diag_snapshot_t sample={.pcb_size=sizeof(struct tcp_pcb)};
    unsigned visited=0;
    for(const struct tcp_pcb *p=tcp_active_pcbs;p;p=p->next){
        if(visited>=TCP_PCB_DIAG_LIMIT){sample.truncated=true;break;}
        visited++;sample.active++;
    }
    for(const struct tcp_pcb *p=tcp_tw_pcbs;p;p=p->next){
        if(visited>=TCP_PCB_DIAG_LIMIT){sample.truncated=true;break;}
        visited++;sample.time_wait++;
        if(p->local_port==80)sample.tw_incoming_http++;
        else if(p->remote_port==502)sample.tw_modbus++;
        else if(p->remote_port==80)sample.tw_remote_http++;
        else if(p->remote_port==12800)sample.tw_shell++;
        else sample.tw_other++;
    }
    for(const struct tcp_pcb *p=tcp_bound_pcbs;p;p=p->next){
        if(visited>=TCP_PCB_DIAG_LIMIT){sample.truncated=true;break;}
        visited++;sample.bound++;
    }
    for(const struct tcp_pcb_listen *p=tcp_listen_pcbs.listen_pcbs;p;p=p->next){
        if(visited>=TCP_PCB_DIAG_LIMIT){sample.truncated=true;break;}
        visited++;sample.listening++;
    }
    sample.pcb_bytes=(sample.active+sample.time_wait+sample.bound)*sizeof(struct tcp_pcb)+sample.listening*sizeof(struct tcp_pcb_listen);
    sample.at=now_ms();sample.available=true;
    /* No application mutex, heap allocation or network operation here. */
    portENTER_CRITICAL(&tcp_pcb_diag_mux);
    sample.samples=tcp_pcb_diag_cache.samples<UINT32_MAX?tcp_pcb_diag_cache.samples+1:UINT32_MAX;
    sample.queue_skips=tcp_pcb_diag_cache.queue_skips;
    tcp_pcb_diag_cache=sample;
    portEXIT_CRITICAL(&tcp_pcb_diag_mux);
}
static void tcp_pcb_diag_start(void){
    /* Called once by status_task, outside state_lock. Keep this one message
       for the lifetime of that task; sampling never allocates another one. */
    if(tcp_pcb_diag_started)return;
    tcp_pcb_diag_started=true;
    tcp_pcb_diag_message=tcpip_callbackmsg_new(tcp_pcb_diag_collect,NULL);
    portENTER_CRITICAL(&tcp_pcb_diag_mux);
    tcp_pcb_diag_cache.available=tcp_pcb_diag_message!=NULL;
    portEXIT_CRITICAL(&tcp_pcb_diag_mux);
}
static void tcp_pcb_diag_schedule(int64_t now){
    if(!tcp_pcb_diag_message||now<tcp_pcb_diag_next)return;
    tcp_pcb_diag_next=now+5000;
    portENTER_CRITICAL(&tcp_pcb_diag_mux);
    bool pending=tcp_pcb_diag_cache.pending;
    if(!pending)tcp_pcb_diag_cache.pending=true;
    portEXIT_CRITICAL(&tcp_pcb_diag_mux);
    if(pending)return;
    if(tcpip_callbackmsg_trycallback(tcp_pcb_diag_message)!=ERR_OK){
        portENTER_CRITICAL(&tcp_pcb_diag_mux);
        tcp_pcb_diag_cache.pending=false;
        if(tcp_pcb_diag_cache.queue_skips<UINT32_MAX)tcp_pcb_diag_cache.queue_skips++;
        portEXIT_CRITICAL(&tcp_pcb_diag_mux);
    }
}
static tcp_pcb_diag_snapshot_t tcp_pcb_diag_read(void){
    tcp_pcb_diag_snapshot_t copy;
    portENTER_CRITICAL(&tcp_pcb_diag_mux);copy=tcp_pcb_diag_cache;portEXIT_CRITICAL(&tcp_pcb_diag_mux);
    return copy;
}
