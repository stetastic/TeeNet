#define _POSIX_C_SOURCE 200809L
#include "ems_core.h"
#include "charge_sessions.h"
#include "cJSON.h"
#include <assert.h>
#include <stdio.h>
#include <math.h>
#include <string.h>
#include <stdlib.h>
#include <time.h>

typedef int esp_err_t;
enum { ESP_OK=0, ESP_FAIL=-1, HTTPD_400_BAD_REQUEST=400, HTTPD_500_INTERNAL_SERVER_ERROR=500 };
typedef struct { const char *uri; size_t chunks,length,fail_chunk; } httpd_req_t;
static char body[256*1024];
static energy_t energy;
static const char mqtt_device_id[]="teenet_1234";
static size_t live,peak;
static bool locked,no_memory;
static int64_t minute=200;
#define LOCK() do{assert(!locked);locked=true;}while(0)
#define UNLOCK() do{assert(locked);locked=false;}while(0)
static int64_t now_ms(void){return minute*60000;}
static unsigned calendar(int64_t *a,int64_t *b){*a=*b=0;return 20261130;}
static int httpd_resp_set_type(httpd_req_t *r,const char *s){(void)r;(void)s;return 0;}
static int httpd_resp_set_hdr(httpd_req_t *r,const char *k,const char *v){(void)r;(void)k;(void)v;return 0;}
static int httpd_resp_send_err(httpd_req_t *r,int status,const char *s){(void)r;(void)s;return status;}
static int httpd_resp_send_chunk(httpd_req_t *r,const char *p,size_t n){
    assert(!locked);if(++r->chunks==r->fail_chunk)return ESP_FAIL;
    assert(r->length+n<sizeof(body));if(n)memcpy(body+r->length,p,n);
    body[r->length+=n]=0;return ESP_OK;
}
static int httpd_resp_sendstr_chunk(httpd_req_t *r,const char *p){return httpd_resp_send_chunk(r,p,strlen(p));}
static size_t httpd_req_get_url_query_len(httpd_req_t *r){const char *q=strchr(r->uri,'?');return q?strlen(q+1):0;}
static int httpd_req_get_url_query_str(httpd_req_t *r,char *buf,size_t size){
    const char *q=strchr(r->uri,'?');if(!q||strlen(q+1)>=size)return ESP_FAIL;strcpy(buf,q+1);return ESP_OK;
}
static int httpd_query_key_value(const char *q,const char *key,char *buf,size_t size){
    size_t k=strlen(key);
    while(*q){
        if(strncmp(q,key,k)==0&&q[k]=='='){
            const char *v=q+k+1;size_t n=strcspn(v,"&");if(n>=size)return ESP_FAIL;
            memcpy(buf,v,n);buf[n]=0;return ESP_OK;
        }
        q=strchr(q,'&');if(!q)break;q++;
    }
    return ESP_FAIL;
}
typedef union{max_align_t align;size_t size;} prefix_t;
static void *tracked_alloc(size_t n){
    assert(!locked);if(no_memory)return NULL;
    prefix_t *p=malloc(sizeof(*p)+n);assert(p);p->size=n;live+=n;
    if(live>peak)peak=live;
    return p+1;
}
static void tracked_free(void *ptr){if(ptr){prefix_t *p=(prefix_t *)ptr-1;assert(!locked&&live>=p->size);live-=p->size;free(p);}}
#include "session_reader_fake.h"
typedef struct{unsigned index;} energy_day_reader_t;
static int bad_day=-1;
static unsigned day_readers;
static int energy_day_reader_open(energy_day_reader_t *r,unsigned today){assert(today==20261130);r->index=0;day_readers++;return ESP_OK;}
static int energy_day_reader_next(energy_day_reader_t *r,energy_day_t *out,bool *found){
    if((int)r->index==bad_day)return ESP_FAIL;
    *found=r->index<30;
    if(*found){*out=(energy_day_t){.date=20261101+r->index,.wh={3000,5000},.covered_ms={30000,30000}};r->index++;}
    return ESP_OK;
}
static void energy_day_reader_close(energy_day_reader_t *r){(void)r;assert(day_readers);day_readers--;}

#define malloc tracked_alloc
#define free tracked_free
#include "session_list_http.inc"
#include "energy_history_http.inc"
#include "session_archive_http.inc"
#undef malloc
#undef free

static cJSON *field(cJSON *object,const char *key){cJSON *f=cJSON_GetObjectItem(object,key);assert(f);return f;}
int main(void){
    for(int i=0;i<EMS_HISTORY;i++){
        int n=200-i;energy.history[n%EMS_HISTORY]=(energy_point_t){.minute=n,.wh={30,45},.covered_ms={60000,60000}};
    }
    energy.history[200%EMS_HISTORY].covered_ms[1]=0;
    for(unsigned i=0;i<512;i++){
        flash_rows[i]=(charge_session_t){.id=i+1,.grid_wh=1500,.solar_wh=500,.cost_eur=.52f,
            .grid_cost_eur=.5f,.solar_cost_eur=.02f,.start_epoch=1700000000+i,
            .start_date=i<200?20261001+i/10:20261101+(i-200)/32};
    }
    active_id=512;flash_rows[40].grid_wh=0; /* 0.5 kWh is not a recorded session. */
    const char *urls[]={"/api/history","/api/export.json","/api/export.lp","/api/export.csv",
        "/api/sessions?offset=20","/api/sessions/day?date=20261110&offset=20"};
    int (*handlers[])(httpd_req_t *)={history_handler,session_archive_handler,session_archive_handler,csv_handler,sessions_handler,session_day_handler};
    for(unsigned route=0;route<6;route++){
        flash_count=512;
        httpd_req_t req={.uri=urls[route]};peak=0;
        assert(handlers[route](&req)==ESP_OK&&live==0&&!locked&&!readers&&!day_readers);
        size_t chunks=req.chunks,full_peak=peak;
        if(route!=2&&route!=3){
            cJSON *tree=cJSON_Parse(body);assert(tree);
            if(route==0){
                cJSON *points=field(tree,"points"),*days=field(tree,"days");
                assert(cJSON_GetArraySize(points)==120&&cJSON_GetArraySize(days)==30);
                assert(field(cJSON_GetArrayItem(points,0),"age_min")->valueint==119);
                assert(field(cJSON_GetArrayItem(points,0),"estimate_w")->valueint==1800);
                assert(cJSON_IsNull(field(cJSON_GetArrayItem(points,119),"wallbox_w")));
                assert(field(cJSON_GetArrayItem(days,0),"date")->valueint==20261101);
                assert(field(cJSON_GetArrayItem(days,29),"date")->valueint==20261130);
            }else if(route==1){
                cJSON *rows=field(tree,"sessions");assert(cJSON_GetArraySize(rows)==510);
                assert(field(cJSON_GetArrayItem(rows,0),"id")->valueint==1&&field(cJSON_GetArrayItem(rows,509),"id")->valueint==511);
            }else if(route==4){
                assert(field(tree,"retained")->valueint==511&&field(tree,"oldest_date")->valueint==20261001);
                assert(field(tree,"count")->valueint==30&&cJSON_GetArraySize(field(tree,"days"))==10);
                assert(field(tree,"grid_kwh")->valuedouble==1.5*511&&field(tree,"solar_kwh")->valuedouble==.5*511);
            }else{
                assert(field(tree,"count")->valueint==24&&cJSON_GetArraySize(field(tree,"sessions"))==4);
                assert(field(cJSON_GetArrayItem(field(tree,"sessions"),0),"id")->valueint==492);
            }
            cJSON_Delete(tree);
        }else if(route==2){assert(strstr(body,"session=511 id=511i")&&!strstr(body,"session=512 id=512i"));}
        else{assert(strstr(body,"SUMME;;;;1022,000000;")&&strstr(body,";Aktiv\r\n")&&strstr(body,"511;"));}

        for(size_t i=1;i<=chunks;i+=chunks>16?chunks/8:1){
            req=(httpd_req_t){.uri=urls[route],.fail_chunk=i};
            assert(handlers[route](&req)==ESP_FAIL&&!live&&!locked&&!readers&&!day_readers);
        }
        req=(httpd_req_t){.uri=urls[route],.fail_chunk=chunks};
        assert(handlers[route](&req)==ESP_FAIL&&!live&&!readers&&!day_readers);
        no_memory=true;req=(httpd_req_t){.uri=urls[route]};
        assert(handlers[route](&req)==500&&!live&&!readers&&!day_readers);no_memory=false;
        bad_day=10;bad_row=43;req=(httpd_req_t){.uri=urls[route]};
        assert(handlers[route](&req)==ESP_FAIL&&!live&&!readers&&!day_readers);bad_day=bad_row=-1;
        flash_count=3;peak=0;
        for(int i=0;i<1000;i++){
            req=(httpd_req_t){.uri=urls[route]};
            assert(handlers[route](&req)==ESP_OK&&!live&&!locked&&!readers&&!day_readers);
        }
        assert(peak==full_peak);
        printf("PASS %s: 3 and 512 records use same HTTP allocation, 1000 calls, read/transfer/allocation failures cleaned up\n",urls[route]);
    }
    minute=0;httpd_req_t r={.uri=urls[0]};assert(history_handler(&r)==ESP_OK&&!live);
    cJSON *tree=cJSON_Parse(body);assert(tree&&cJSON_GetArraySize(field(tree,"points"))==1);cJSON_Delete(tree);
}
