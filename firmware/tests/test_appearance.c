#include "cJSON.h"
#include <stdint.h>
#include <stdbool.h>
#include <string.h>
#include <stdio.h>
#include <assert.h>
typedef int esp_err_t;
typedef int nvs_handle_t;
#define ESP_OK 0
#define NVS_READONLY 0
#define NVS_READWRITE 1
#define HTTPD_500_INTERNAL_SERVER_ERROR 500
#define HTTPD_400_BAD_REQUEST 400
typedef struct {const char *body;bool allowed;int status;cJSON *response;} httpd_req_t;
static uint8_t theme,view,density,seen;static bool fail_commit;
static esp_err_t nvs_open(const char *name,int mode,nvs_handle_t *handle){(void)name;(void)mode;*handle=1;return 0;}
static uint8_t *value(const char *key){if(!strcmp(key,"ui_theme"))return &theme;if(!strcmp(key,"ui_view"))return &view;if(!strcmp(key,"ui_density"))return &density;assert(!strcmp(key,"ui_graph_seen"));return &seen;}
static esp_err_t nvs_get_u8(nvs_handle_t h,const char *key,uint8_t *v){(void)h;*v=*value(key);return 0;}
static esp_err_t nvs_set_u8(nvs_handle_t h,const char *key,uint8_t v){(void)h;*value(key)=v;return 0;}
static esp_err_t nvs_commit(nvs_handle_t h){(void)h;return fail_commit?1:0;}
static void nvs_close(nvs_handle_t h){(void)h;}
static bool authorized(httpd_req_t *r){if(!r->allowed)r->status=403;return r->allowed;}
static cJSON *read_json(httpd_req_t *r){return cJSON_Parse(r->body);}
static esp_err_t send_json(httpd_req_t *r,cJSON *o){r->status=200;r->response=o;return 0;}
static esp_err_t httpd_resp_send_err(httpd_req_t *r,int code,const char *message){(void)message;r->status=code;return 0;}
#include "appearance_actual.inc"
static int post(const char *body,bool allow){httpd_req_t r={.body=body,.allowed=allow};theme_post(&r);cJSON_Delete(r.response);return r.status;}
int main(void){
 httpd_req_t r={0};theme_get(&r);assert(cJSON_IsTrue(cJSON_GetObjectItem(r.response,"graphic_notice_pending")));assert(!strcmp(cJSON_GetObjectItem(r.response,"density")->valuestring,"standard"));cJSON_Delete(r.response);
 assert(post("{\"view\":\"graphic\"}",true)==200&&view==1&&theme==0&&seen==0);
 assert(post("{\"theme\":\"dark\"}",true)==200&&view==1&&theme==2&&seen==0);
 assert(post("{\"density\":\"compact\"}",false)==403&&density==0);
 assert(post("{\"density\":\"compact\"}",true)==200&&density==1&&view==1&&theme==2&&seen==0);
 assert(post("{\"density\":\"invalid\"}",true)==400&&density==1);
 assert(post("{\"density\":true}",true)==400&&density==1);
 assert(post("{\"density\":\"standard\",\"view\":\"standard\"}",true)==400&&density==1&&view==1);
 r=(httpd_req_t){0};theme_get(&r);assert(!strcmp(cJSON_GetObjectItem(r.response,"density")->valuestring,"compact"));cJSON_Delete(r.response);
 assert(post("{\"density\":\"standard\"}",true)==200&&density==0&&view==1&&theme==2);
 assert(post("{\"graphic_notice_ack\":true}",false)==403&&seen==0);
 assert(post("{\"graphic_notice_ack\":false}",true)==400&&seen==0);
 assert(post("{\"view\":\"invalid\"}",true)==400&&view==1);
 assert(post("{\"graphic_notice_ack\":true,\"view\":\"standard\"}",true)==400&&seen==0);
 assert(post("{\"graphic_notice_ack\":true}",true)==200&&seen==1&&view==1&&theme==2);
 r=(httpd_req_t){0};theme_get(&r);assert(cJSON_IsFalse(cJSON_GetObjectItem(r.response,"graphic_notice_pending")));cJSON_Delete(r.response);
 fail_commit=true;assert(post("{\"view\":\"standard\"}",true)==500);
 puts("PASS: appearance defaults, independent settings, persistent notice acknowledgement, auth, validation and storage errors");return 0;
}
