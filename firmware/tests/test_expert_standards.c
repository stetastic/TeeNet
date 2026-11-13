#include "ems_core.h"
#include "expert_standards.h"
#include "cJSON.h"
#include <assert.h>
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
typedef int esp_err_t;typedef int nvs_handle_t;
enum{ESP_OK=0,ESP_ERR_INVALID_STATE=1,ESP_FAIL=2,NVS_READONLY=0,NVS_READWRITE=1,HTTPD_400_BAD_REQUEST=400,HTTPD_500_INTERNAL_SERVER_ERROR=500};
#define pdTRUE 1
#define ticks(x) (x)
typedef struct{int status;}httpd_req_t;
static bool config_storage_ok=true,stopped=true,fail_write;static int maintenance_flash_lock,locks,flash_lock;
#define LOCK() assert(locks++==0)
#define UNLOCK() assert(--locks==0)
static bool expert_stopped_locked(void){assert(locks==1);return stopped;}
static int xSemaphoreTake(int l,int t){(void)l;(void)t;assert(!locks&&!flash_lock);flash_lock=1;return pdTRUE;}
static void xSemaphoreGive(int l){(void)l;assert(!locks&&flash_lock);flash_lock=0;}
static char keys[4][12];static unsigned char blobs[4][80];static size_t sizes[4];static unsigned entries;
static int nvs_open(const char *name,int mode,nvs_handle_t *n){assert(!strcmp(name,"expert_std"));(void)mode;*n=1;return ESP_OK;}
static int nvs_get_blob(int n,const char *key,void *out,size_t *size){(void)n;for(unsigned i=0;i<entries;i++)if(!strcmp(keys[i],key)){assert(*size>=sizes[i]);memcpy(out,blobs[i],sizes[i]);*size=sizes[i];return ESP_OK;}return ESP_FAIL;}
static int nvs_set_blob(int n,const char *key,const void *data,size_t size){(void)n;assert(flash_lock&&!locks&&size<=80);if(fail_write)return ESP_FAIL;unsigned i=0;for(;i<entries&&strcmp(keys[i],key);i++){}assert(i<4);if(i==entries)entries++;strcpy(keys[i],key);memcpy(blobs[i],data,size);sizes[i]=size;return ESP_OK;}
static int nvs_commit(int n){(void)n;return ESP_OK;}static void nvs_close(int n){(void)n;}
static int httpd_resp_send_err(httpd_req_t*r,int c,const char*s){(void)s;r->status=c;return 0;}
static void httpd_resp_set_status(httpd_req_t*r,const char*s){r->status=atoi(s);}
static int httpd_resp_sendstr(httpd_req_t*r,const char*s){(void)r;(void)s;return 0;}
static int ok_json(httpd_req_t*r){r->status=200;return 0;}
#include "expert_standards.inc"
static int post(const char *body){httpd_req_t r={0};expert_standard_post(&r,cJSON_Parse(body));assert(!locks&&!flash_lock);return r.status;}
int main(void){
 expert_values_t actual;expert_defaults(&actual);assert(expert_activate(&actual));float factory=expert_meta[EP_PHASE_SETTLE].standard;
 assert(expert_standard_value(EP_PHASE_SETTLE)==factory);
 assert(post("{\"pin\":\"4040\",\"action\":\"standard\",\"id\":\"PHASE_SETTLE\",\"value\":3}")==200);
 assert(expert_standard_value(EP_PHASE_SETTLE)==3&&expert_meta[EP_PHASE_SETTLE].standard==factory&&ems_param(EP_PHASE_SETTLE)==factory);
 assert(post("{\"pin\":\"4040\",\"action\":\"standard\",\"id\":\"wallbox_tx_pin\",\"value\":8}")==200&&gpio_standard_value(0)==8);
 memset(expert_standard_set,0,sizeof(expert_standard_set));memset(gpio_standard_set,0,sizeof(gpio_standard_set));expert_standards_load();
 assert(expert_standard_value(EP_PHASE_SETTLE)==3&&gpio_standard_value(0)==8);
 settings_t cfg;settings_defaults(&cfg);settings_gpio_set(&cfg,0,8);actual.values[EP_PHASE_SETTLE]=3;
 assert(!expert_standards_modified(&actual,&cfg));actual.values[EP_PHASE_SETTLE]=4;assert(expert_standards_modified(&actual,&cfg));
 actual.values[EP_PHASE_SETTLE]=3;strcpy(cfg.wallbox_meter_type,"shelly_gen2");
 cfg.xemex_tx_pin=cfg.xemex_rx_pin=8;assert(!settings_gpio_used(&cfg,2)&&!expert_standards_modified(&actual,&cfg));
 cfg.status_led_pin=-1;assert(expert_standards_modified(&actual,&cfg));cfg.status_led_pin=gpio_standard_value(EMS_GPIO_ROLES-1);
 strcpy(cfg.wallbox_meter_type,"xemex");assert(settings_gpio_used(&cfg,2)&&expert_standards_modified(&actual,&cfg));
 fail_write=true;assert(post("{\"pin\":\"4040\",\"action\":\"standard\",\"id\":\"PHASE_SETTLE\",\"value\":4}")==500&&expert_standard_value(EP_PHASE_SETTLE)==3);fail_write=false;
 assert(post("{\"pin\":\"4040\",\"action\":\"standard\",\"id\":\"PHASE_SETTLE\",\"value\":0}")==400);
 assert(post("{\"pin\":\"4040\",\"action\":\"standard\",\"id\":\"unknown\",\"value\":3}")==400);
 assert(post("{\"pin\":\"4040\",\"action\":\"standard\",\"id\":\"wallbox_tx_pin\",\"value\":0}")==400);
 stopped=false;assert(post("{\"pin\":\"4040\",\"action\":\"standard\",\"id\":\"PHASE_SETTLE\",\"value\":4}")==500&&expert_standard_value(EP_PHASE_SETTLE)==3);
 char a[12],b[12];for(unsigned i=0;i<EP_COUNT;i++)for(unsigned j=0;j<i;j++){expert_standard_key(expert_meta[i].id,false,a);expert_standard_key(expert_meta[j].id,false,b);assert(strcmp(a,b));}
 puts("PASS: personal parameter/GPIO standards persist by stable ID, factory and active values unchanged, invalid input/flash failure rejected, no hash collisions");
}
