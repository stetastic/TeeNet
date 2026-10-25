#include "http_encoding.h"
#include <assert.h>
#include <stdio.h>
#include <stdint.h>
typedef int esp_err_t;
#define ESP_OK 0
#define ESP_ERR_NOT_FOUND 1
#define EMS_BUILD_ID "test"
#define ASSET(name,n) static const uint8_t name##_start[]={n,1,2,3,0};static const uint8_t *name##_end=name##_start+5
ASSET(energy_gz,16);
ASSET(html_gz,1);ASSET(css_gz,2);ASSET(js_gz,3);ASSET(health_gz,4);ASSET(firmware_gz,5);ASSET(terms_js_gz,6);ASSET(evcc_js_gz,7);ASSET(features_gz,8);ASSET(qr_gz,9);ASSET(guide_gz,10);ASSET(guide_js_gz,11);ASSET(expert_schema_gz,12);ASSET(logo,13);ASSET(hardware,14);ASSET(wiring,15);
typedef struct {const char *uri,*encoding,*cached,*data;size_t bytes;char type[64],etag[128];bool gzip;int status;} httpd_req_t;
static esp_err_t httpd_req_get_hdr_value_str(httpd_req_t *r,const char *name,char *out,size_t capacity){const char *v=!strcmp(name,"Accept-Encoding")?r->encoding:r->cached;if(!v)return ESP_ERR_NOT_FOUND;if(strlen(v)>=capacity)return 2;strcpy(out,v);return 0;}
static void httpd_resp_set_hdr(httpd_req_t *r,const char *name,const char *v){if(!strcmp(name,"Content-Encoding"))r->gzip=!strcmp(v,"gzip");if(!strcmp(name,"ETag"))strcpy(r->etag,v);}
static void httpd_resp_set_status(httpd_req_t *r,const char *status){r->status=atoi(status);}
static void httpd_resp_set_type(httpd_req_t *r,const char *type){snprintf(r->type,sizeof(r->type),"%s",type);}
static esp_err_t httpd_resp_send(httpd_req_t *r,const char *data,size_t length){r->data=data;r->bytes=length;return 0;}
static esp_err_t httpd_resp_sendstr(httpd_req_t *r,const char *data){return httpd_resp_send(r,data,strlen(data));}
#include "asset_actual.inc"
int main(void){
 const char *uris[]={"/","/dashboard.css","/dashboard.js","/health.js","/firmware.js","/terms.js","/evcc-test.js","/features.js","/qrcode.js","/guide","/guide.js","/expert-schema.json"};
 for(unsigned i=0;i<12;i++){httpd_req_t r={.uri=uris[i],.encoding="br, gzip;q=0.5, identity;q=0"};assert(asset_send(&r)==0&&r.gzip&&r.bytes==5&&(unsigned char)r.data[0]==i+1);}
 httpd_req_t r={.uri="/energy.js",.encoding="gzip"};asset_send(&r);assert(r.gzip&&r.bytes==5&&(unsigned char)r.data[0]==16&&!strcmp(r.type,"application/javascript; charset=utf-8"));
 r=(httpd_req_t){.uri="/",.encoding="identity"};asset_send(&r);assert(r.status==406&&!r.gzip&&strstr(r.data,"GZIP"));
 r=(httpd_req_t){.uri="/"};asset_send(&r);assert(r.gzip&&r.bytes==5);char etag[128];strcpy(etag,r.etag);
 r=(httpd_req_t){.uri="/",.encoding="gzip",.cached=etag};asset_send(&r);assert(r.status==304&&!r.bytes);
 const char *images[]={"/teennet-logo.png","/hardware-plan.svg","/wiring-plan.jpg"};for(unsigned i=0;i<3;i++){r=(httpd_req_t){.uri=images[i],.encoding="identity"};asset_send(&r);assert(!r.gzip&&r.bytes==4&&(unsigned char)r.data[0]==i+13);}
 puts("PASS: actual HTTP asset handler: 13 complete compressed routes, explicit refusal, quality negotiation, absent header, cache 304 and unchanged image bytes");return 0;
}
