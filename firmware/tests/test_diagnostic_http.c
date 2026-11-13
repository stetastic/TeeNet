#include "ems_features.h"
#include "diagnostic_store.h"
#include "json_owned.h"
#include <assert.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/types.h>
typedef int esp_err_t;
#define ESP_OK 0
#define ESP_FAIL -1
#define pdTRUE 1
#define ticks(x) (x)
#define HTTPD_RESP_USE_STRLEN -1
typedef struct{int status;bool complete;}httpd_req_t;
static ems_events_t event_log;static diagnostic_store_t fault_log;static bool fault_storage_ok=true,busy;
static int status_snapshot_lock=1,held,locks;
static size_t used,peak,calls,fail_at,limit=8192,output_bytes;static char output[65536],expected[65536];
static size_t sends,fail_send_at,data_chunks,max_chunk;
typedef union{max_align_t alignment;size_t bytes;}allocation_t;
static void *allocate(size_t n){if(++calls==fail_at||n>limit-used)return NULL;allocation_t*p=malloc(sizeof(*p)+n);assert(p);p->bytes=n;used+=n;if(used>peak)peak=used;return p+1;}
static void release(void*v){if(v){allocation_t*p=(allocation_t*)v-1;used-=p->bytes;free(p);}}
#define LOCK() assert(locks++==0)
#define UNLOCK() assert(--locks==0)
static int xSemaphoreTake(int id,int wait){assert(id==1&&wait==1000&&!held&&!locks);if(busy)return 0;held=1;return pdTRUE;}
static void xSemaphoreGive(int id){assert(id==1&&held&&!locks);held=0;}
static void httpd_resp_set_status(httpd_req_t*r,const char*s){r->status=atoi(s);}
static int httpd_resp_sendstr(httpd_req_t*r,const char*s){(void)s;r->complete=true;return ESP_OK;}
static void httpd_resp_set_type(httpd_req_t*r,const char*s){(void)r;(void)s;}
static void httpd_resp_set_hdr(httpd_req_t*r,const char*k,const char*v){(void)r;(void)k;(void)v;}
static int httpd_resp_send_chunk(httpd_req_t*r,const char*b,ssize_t n){
 assert(held&&!locks);if(++sends==fail_send_at)return ESP_FAIL;
 if(n<0)n=strlen(b);if(!n){r->complete=true;output[output_bytes]=0;return ESP_OK;}
 data_chunks++;if((size_t)n>max_chunk)max_chunk=(size_t)n;
 assert((size_t)n<=1024&&output_bytes+(size_t)n<sizeof(output));memcpy(output+output_bytes,b,n);output_bytes+=n;return ESP_OK;
}
static void nullable(cJSON*o,const char*k,double value,bool valid){if(valid)cJSON_AddNumberToObject(o,k,value);else cJSON_AddNullToObject(o,k);}
#define malloc allocate
#define free release
#include "diagnostic_http.inc"
#undef malloc
#undef free
static void reset_request(void){
 assert(!used&&!held&&!locks);calls=output_bytes=sends=data_chunks=max_chunk=0;peak=0;output[0]=0;
}
static void make_expected(void){
 /* Independent legacy JSON structure; byte-for-byte comparison detects any
    change in row ordering, formatting, escaping, nulls or storage status. */
 size_t previous_limit=limit;limit=1000000;
 cJSON *root=cJSON_CreateObject(),*events=cJSON_AddArrayToObject(root,"events"),*faults=cJSON_AddArrayToObject(root,"persisted_faults");
 for(unsigned i=0;i<event_log.count;i++)assert(json_add_owned_to_array(events,event_json(ems_event_recent(&event_log,i))));
 for(unsigned i=0;i<fault_log.count;i++)assert(json_add_owned_to_array(faults,event_json(diagnostic_store_recent(&fault_log,i))));
 cJSON_AddBoolToObject(root,"storage_ok",fault_storage_ok);
 char *text=cJSON_PrintUnformatted(root);assert(text&&strlen(text)<sizeof(expected));strcpy(expected,text);
 cJSON_free(text);cJSON_Delete(root);assert(!used);limit=previous_limit;
}
static void fixture(unsigned events,unsigned faults,bool saved){
 memset(&event_log,0,sizeof(event_log));memset(&fault_log,0,sizeof(fault_log));
 event_log.count=events;event_log.next=events%EMS_EVENT_CAPACITY;
 for(unsigned i=0;i<events;i++){
  event_log.entries[i]=(ems_event_t){.sequence=i+1,.uptime_s=i,.actual_kw=i?6:NAN,.target_kw=i*0.125};
  strcpy(event_log.entries[i].reason,i%2?"feedback":"quoted\" slash\\ newline\n");
 }
 fault_log.count=faults;fault_log.next=faults%EMS_FAULT_CAPACITY;
 for(unsigned i=0;i<faults;i++){fault_log.entries[i]=(ems_event_t){.sequence=100+i,.epoch_s=1800000000+i};strcpy(fault_log.entries[i].reason,"meter");}
 fault_storage_ok=saved;
}
static void check_fixture(unsigned events,unsigned faults,bool saved){
 fixture(events,faults,saved);make_expected();reset_request();
 httpd_req_t req={.status=200};assert(events_handler(&req)==ESP_OK&&req.complete&&!used&&!held&&!locks);
 assert(!strcmp(output,expected));assert(max_chunk<=1024);
 assert(data_chunks==(output_bytes+1023)/1024&&sends==data_chunks+1&&peak<8192);
}
int main(void){
 cJSON_Hooks hooks={allocate,release};cJSON_InitHooks(&hooks);
 check_fixture(0,0,false);check_fixture(1,0,true);check_fixture(0,1,false);check_fixture(16,16,true);
 check_fixture(EMS_EVENT_CAPACITY,EMS_FAULT_CAPACITY,true);
 size_t bounded_peak=peak,allocations=calls,bytes=output_bytes,chunks=data_chunks,send_count=sends;
 for(size_t i=1;i<=allocations;i++){
  reset_request();fail_at=i;httpd_req_t req={.status=200};int result=events_handler(&req);
  assert(!used&&!held&&!locks);if(i<=2)assert(result==ESP_OK&&req.status==503);else assert(result==ESP_FAIL&&!req.complete);
 }
 fail_at=0;
 /* Every data flush and final zero chunk may fail. Never finish an incomplete
    response, continue sending after failure, retain memory or retain a lock. */
 for(size_t i=1;i<=send_count;i++){
  reset_request();fail_send_at=i;httpd_req_t req={.status=200};
  assert(events_handler(&req)==ESP_FAIL&&!req.complete&&!used&&!held&&!locks&&sends==i);
 }
 fail_send_at=0;reset_request();busy=true;httpd_req_t req={.status=200};
 assert(events_handler(&req)==ESP_OK&&req.status==503&&!used&&!held&&!locks&&!sends);busy=false;
 /* The append helper must also split a value larger than its buffer without
    overrunning or changing bytes, even if future fields become larger. */
 reset_request();char long_value[2501];memset(long_value,'x',sizeof(long_value));held=1;
 req=(httpd_req_t){.status=200};event_output_t stream={.req=&req};
 assert(event_output_append(&stream,long_value,sizeof(long_value))==ESP_OK&&event_output_flush(&stream)==ESP_OK);
 assert(output_bytes==sizeof(long_value)&&!memcmp(output,long_value,sizeof(long_value))&&data_chunks==3&&max_chunk==1024);held=0;
 cJSON_InitHooks(NULL);
 printf("PASS: all %u records preserve exact JSON bytes/order; %zu bytes use %zu data chunks plus terminator; %zu-byte heap peak + %zu-byte stack buffer; all %zu allocation and %zu send failures release memory/locks and reject incomplete output\n",EMS_EVENT_CAPACITY+EMS_FAULT_CAPACITY,bytes,chunks,bounded_peak,sizeof(stream),allocations,send_count);
}
