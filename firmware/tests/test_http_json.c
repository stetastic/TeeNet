#include "cJSON.h"
#include <assert.h>
#include <stdbool.h>
#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <stdint.h>
typedef int httpd_err_code_t;
typedef struct {size_t content_len;void *handle;} httpd_req_t;
#define HTTPD_400_BAD_REQUEST 400
#define HTTPD_408_REQ_TIMEOUT 408
#define HTTPD_500_INTERNAL_SERVER_ERROR 500
#define HTTPD_SOCK_ERR_TIMEOUT -2
#define SHUT_RDWR 2
static int64_t clock_ms;
static unsigned reads,closed,buffers;
static int error,mode;
static size_t position;
static const char *payload;
static bool allocation_failure;
static int64_t now_ms(void){return clock_ms;}
static int httpd_resp_set_hdr(httpd_req_t *req,const char *name,const char *value){(void)req;assert(!strcmp(name,"Connection")&&!strcmp(value,"close"));return 0;}
static int httpd_resp_send_err(httpd_req_t *req,int status,const char *text){(void)req;(void)text;error=status;return 0;}
static int httpd_req_to_sockfd(httpd_req_t *req){(void)req;return 7;}
static int shutdown(int fd,int how){assert(fd==7&&how==2);closed++;return 0;}
static int httpd_req_recv(httpd_req_t *req,char *dest,size_t capacity){
    (void)req;reads++;
    if(mode==1){clock_ms+=2000;return HTTPD_SOCK_ERR_TIMEOUT;}
    if(mode==2)return -1;
    size_t count=mode==3?1:strlen(payload)-position;
    if(count>capacity)count=capacity;
    clock_ms+=mode==3?1500:1;
    memcpy(dest,payload+position,count);position+=count;return (int)count;
}
static void *test_malloc(size_t size){if(allocation_failure)return NULL;void *ptr=malloc(size);if(ptr)buffers++;return ptr;}
static void test_free(void *ptr){if(ptr){assert(buffers);buffers--;free(ptr);}}
#define malloc test_malloc
#define free test_free
#include "http_json.inc"
#undef malloc
#undef free
static void fixture(const char *body,int selected){payload=body;mode=selected;position=clock_ms=reads=closed=buffers=error=0;allocation_failure=false;}
int main(void){
    httpd_req_t req={.content_len=11};
    fixture("{\"value\":1}",0);cJSON *object=read_json(&req);assert(object&&!closed&&!buffers);cJSON_Delete(object);
    fixture("................................",3);req.content_len=32;
    assert(!read_json(&req)&&error==408&&closed==1&&clock_ms==4500&&reads==3&&!buffers);
    fixture("",1);assert(!read_json(&req)&&error==408&&clock_ms==4000&&reads==2&&closed==1&&!buffers);
    fixture("",2);assert(!read_json(&req)&&error==408&&closed==1&&!buffers);
    fixture("{\"a\":1,\"a\":2}",0);req.content_len=strlen(payload);assert(!read_json(&req)&&error==400&&closed==1&&!buffers);
    fixture("[]",0);req.content_len=2;assert(!read_json(&req)&&error==400&&closed==1&&!buffers);
    fixture("",0);req.content_len=8193;assert(!read_json(&req)&&error==400&&!reads&&closed==1);
    fixture("{}",0);req.content_len=2;allocation_failure=true;assert(!read_json(&req)&&error==500&&!reads&&closed==1&&!buffers);
    puts("PASS: real JSON receiver: absolute 4-second budget, drip at 4.5 seconds stopped, two 2-second timeouts, disconnect/invalid/duplicate/OOM closed before IDF purge, no leaked body buffer");
}
