#include "cJSON.h"
#include "firmware_check.h"
#include <assert.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdlib.h>
#include <stdio.h>
#include <string.h>

typedef int esp_err_t;
typedef struct request { size_t content_len;const void *user_ctx;int status; } httpd_req_t;
typedef struct { esp_err_t (*handler)(httpd_req_t *); } httpd_uri_t;
typedef struct { size_t size; } esp_partition_t;
typedef unsigned esp_ota_handle_t;
#define ESP_OK 0
#define HTTPD_400_BAD_REQUEST 400
#define HTTPD_408_REQ_TIMEOUT 408
#define HTTPD_500_INTERNAL_SERVER_ERROR 500
#define HTTPD_SOCK_ERR_TIMEOUT -2
#define SHUT_RDWR 2
#define EMS_METER_TTL 5000
static unsigned closed,headers,reads,begins,aborts,ends,boots;
static int mode,locks;
static int64_t clock_ms,actual_at;
static bool auth_ok,prefix_ok,restarting,ota_in_progress;
static struct { bool enabled; } settings;
static float actual_a[3];
static esp_partition_t partition={.size=2*1024*1024};
#define LOCK() assert(locks++==0)
#define UNLOCK() assert(--locks==0)
static int64_t now_ms(void){return clock_ms;}
static bool fresh(int64_t now,int64_t at,int64_t ttl){return at>0&&now>=at&&now-at<ttl;}
static bool authorized(httpd_req_t *req){(void)req;return auth_ok;}
static int httpd_resp_set_hdr(httpd_req_t *req,const char *name,const char *value){(void)req;assert(!strcmp(name,"Connection")&&!strcmp(value,"close"));headers++;return ESP_OK;}
static int httpd_req_to_sockfd(httpd_req_t *req){(void)req;return 7;}
static int shutdown(int fd,int how){assert(fd==7&&how==SHUT_RDWR);closed++;return 0;}
static int httpd_resp_send_err(httpd_req_t *req,int status,const char *text){(void)text;assert(headers);req->status=status;return ESP_OK;}
static int httpd_resp_set_status(httpd_req_t *req,const char *status){req->status=!strncmp(status,"409",3)?409:500;return ESP_OK;}
static int httpd_resp_sendstr(httpd_req_t *req,const char *text){(void)req;(void)text;assert(headers);return ESP_OK;}
static int send_json(httpd_req_t *req,cJSON *o){assert(headers);req->status=200;cJSON_Delete(o);return ESP_OK;}
static const esp_partition_t *esp_ota_get_next_update_partition(const void *running){(void)running;return &partition;}
static int httpd_req_recv(httpd_req_t *req,char *dest,size_t capacity){
    (void)req;assert(headers&&!closed);reads++;
    if(mode==1){clock_ms+=1500;dest[0]=0;return 1;}
    if(mode==2){clock_ms+=2000;return HTTPD_SOCK_ERR_TIMEOUT;}
    if(mode==3)return -1;
    memset(dest,0,capacity);clock_ms++;return (int)capacity;
}
bool firmware_prefix_check(const uint8_t *data,size_t size,char version[32]){(void)data;assert(size==EMS_FIRMWARE_PREFIX);strcpy(version,"1.19");return prefix_ok;}
static int esp_ota_begin(const esp_partition_t *p,size_t size,esp_ota_handle_t *handle){assert(p==&partition&&size<=p->size);begins++;*handle=1;return ESP_OK;}
static int esp_ota_write(esp_ota_handle_t handle,const void *data,size_t size){(void)data;assert(handle==1&&size>0);return ESP_OK;}
static int esp_ota_abort(esp_ota_handle_t handle){assert(handle==1);aborts++;return ESP_OK;}
static int esp_ota_end(esp_ota_handle_t handle){assert(handle==1);ends++;return ESP_OK;}
static int esp_ota_set_boot_partition(const esp_partition_t *p){assert(p==&partition);boots++;return ESP_OK;}
#include "ota_http.inc"
#include "http_post_guard.inc"
static int early_rejection(httpd_req_t *req){assert(headers&&!reads&&!closed);req->status=409;return ESP_OK;}
static void fixture(void){closed=headers=reads=begins=aborts=ends=boots=0;clock_ms=10000;actual_at=9000;mode=locks=0;auth_ok=prefix_ok=true;restarting=ota_in_progress=settings.enabled=false;memset(actual_a,0,sizeof(actual_a));}
int main(void){
    static const httpd_uri_t ota={.handler=ota_handler},early={.handler=early_rejection};
    httpd_req_t req={.content_len=1024,.user_ctx=&ota};
    fixture();req.content_len=2147483647;req.user_ctx=&early;assert(http_post_guard(&req)==ESP_OK&&req.status==409&&closed==1&&!reads);
    fixture();req.user_ctx=&ota;assert(http_post_guard(&req)==ESP_OK&&req.status==400&&closed==1&&!reads&&!begins);
    fixture();req.content_len=1024;auth_ok=false;assert(http_post_guard(&req)==ESP_OK&&closed==1&&!reads&&!begins);
    fixture();mode=1;assert(http_post_guard(&req)==ESP_OK&&req.status==408&&closed==1&&reads==3&&clock_ms==14500&&!begins);
    fixture();mode=2;assert(http_post_guard(&req)==ESP_OK&&req.status==408&&closed==1&&reads==2&&clock_ms==14000&&!begins);
    fixture();prefix_ok=false;assert(http_post_guard(&req)==ESP_OK&&req.status==400&&closed==1&&reads==1&&!begins);
    fixture();settings.enabled=true;assert(http_post_guard(&req)==ESP_OK&&req.status==409&&closed==1&&reads==1&&!begins);
    fixture();assert(http_post_guard(&req)==ESP_OK&&req.status==200&&closed==1&&begins==1&&ends==1&&boots==1&&!aborts&&restarting&&!ota_in_progress&&!locks);
    puts("PASS: POST guard closes unread bodies after early rejection and successful upload; real OTA prefix loop rejects 1-byte/1.5-second drip at 4.5 s and two receive timeouts at 4 s before touching flash");
}
