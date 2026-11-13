#include "vehicle_profiles.h"
#include "json_owned.h"
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stddef.h>
#include <stdint.h>
typedef int nvs_handle_t;typedef int esp_err_t;
enum {ESP_OK=0,ESP_ERR_NVS_NOT_FOUND=1,NVS_READONLY=0,NVS_READWRITE=1,pdTRUE=1};
static unsigned char disk[8192],staged[8192];static size_t disk_size,staged_size;static bool fail_commit;
static bool stats_storage_ok=true,storage_error;static vehicle_store_t vehicles={.version=1};static unsigned vehicle_revision;static int64_t vehicle_soc_at[4],car_soc_at;
static int maintenance_flash_lock,locks;
#define LOCK() assert(locks++==0)
#define UNLOCK() assert(--locks==0)
static int ticks(int n){return n;}static int xSemaphoreTake(int a,int b){(void)a;(void)b;return pdTRUE;}static void xSemaphoreGive(int a){(void)a;}
static int nvs_open_from_partition(const char *p,const char *n,int mode,int *h){assert(!strcmp(p,"stats")&&!strcmp(n,"vehicles"));(void)mode;*h=1;return ESP_OK;}
static int nvs_get_blob(int h,const char *key,void *data,size_t *n){assert(h==1);if(strcmp(key,"bundle_v1")||!disk_size)return ESP_ERR_NVS_NOT_FOUND;if(!data){*n=disk_size;return ESP_OK;}if(*n<disk_size)return 2;memcpy(data,disk,disk_size);*n=disk_size;return ESP_OK;}
static int nvs_set_blob(int h,const char *key,const void *data,size_t n){assert(h==1&&!strcmp(key,"bundle_v1")&&n<sizeof(staged));memcpy(staged,data,n);staged_size=n;return ESP_OK;}
static int nvs_commit(int h){assert(h==1);if(fail_commit){staged_size=0;return 2;}if(staged_size){memcpy(disk,staged,staged_size);disk_size=staged_size;staged_size=0;}return ESP_OK;}
static int nvs_erase_key(int h,const char *key){assert(h==1&&strncmp(key,"photo",5)==0);return ESP_OK;}
static void nvs_close(int h){assert(h==1);}
static const char alphabet[]="ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";
static int mbedtls_base64_encode(unsigned char *out,size_t cap,size_t *n,const unsigned char *in,size_t len){
 size_t need=(len+2)/3*4;if(cap<=need)return -1;size_t j=0;for(size_t i=0;i<len;i+=3){unsigned v=(unsigned)in[i]<<16;if(i+1<len)v|=(unsigned)in[i+1]<<8;if(i+2<len)v|=in[i+2];out[j++]=alphabet[v>>18];out[j++]=alphabet[(v>>12)&63];out[j++]=i+1<len?alphabet[(v>>6)&63]:'=';out[j++]=i+2<len?alphabet[v&63]:'=';}out[j]=0;*n=j;return 0;
}
static int mbedtls_base64_decode(unsigned char *out,size_t cap,size_t *n,const unsigned char *in,size_t len){
 if(len%4)return -1;size_t j=0;for(size_t i=0;i<len;i+=4){unsigned v=0;for(unsigned k=0;k<4;k++){const char *p=in[i+k]=='='?alphabet:strchr(alphabet,in[i+k]);if(!p)return -1;v=v*64+(unsigned)(p-alphabet);}for(unsigned k=0;k<3;k++)if(in[i+k+1]!='='){if(j>=cap)return -1;out[j++]=(unsigned char)(v>>(16-8*k));}}*n=j;return 0;
}
static long fail_at=-1;static unsigned calls,live;
typedef union {max_align_t alignment;size_t size;} allocation_t;
static void *checked_malloc(size_t n){if(fail_at>=0&&calls++==(unsigned)fail_at)return NULL;allocation_t *p=malloc(sizeof(*p)+n);if(!p)return NULL;live++;return p+1;}
static void checked_free(void *p){if(p){assert(live);live--;free((allocation_t *)p-1);}}
static void *checked_calloc(size_t count,size_t n){void *p=checked_malloc(count*n);if(p)memset(p,0,count*n);return p;}
#define malloc checked_malloc
#define free checked_free
#define calloc checked_calloc
#include "vehicle_storage.inc"
#undef malloc
#undef free
#undef calloc
int main(void){
 cJSON_Hooks hooks={checked_malloc,checked_free};cJSON_InitHooks(&hooks);
 vehicles.rows[0]=(vehicle_profile_t){.name="Saved car",.plate="AB 123",.cards={"CARD",""},.capacity_kwh=60,.maximum_kw=11,.efficiency=.9,.target_soc=80,.picture=3};
 vehicle_assets_t *a=vehicle_assets_read();assert(a&&a->store.rows[0].picture==3);assert(vehicle_assets_commit(a));assert(disk_size<1024);
 a->photo_size[0]=1536;a->photos[0][0]=0xff;a->photos[0][1]=0xd8;a->photos[0][1534]=0xff;a->photos[0][1535]=0xd9;assert(vehicle_assets_commit(a));checked_free(a);assert(disk_size<3000);
 cJSON *json=vehicle_assets_json();assert(json);char *text=cJSON_PrintUnformatted(json);assert(text);vehicle_assets_t *parsed=vehicle_assets_parse(json);assert(parsed&&parsed->photo_size[0]==1536&&parsed->store.rows[0].picture==3);checked_free(parsed);checked_free(text);cJSON_Delete(json);assert(!live);
 a=vehicle_assets_read();strcpy(a->store.rows[0].name,"Different");fail_commit=true;assert(!vehicle_assets_commit(a));fail_commit=false;checked_free(a);a=vehicle_assets_read();assert(!strcmp(a->store.rows[0].name,"Saved car"));checked_free(a);
 vehicle_assets_boot();assert(!storage_error&&vehicles.rows[0].picture==3);
 a=vehicle_assets_read();vehicle_restore_pending=a;assert(vehicle_restore_commit());assert(vehicle_revision==1);vehicle_restore_pending=NULL;checked_free(a);
 unsigned failures=0;for(unsigned f=0;f<550;f++){calls=0;fail_at=f;json=vehicle_assets_json();if(!json)failures++;cJSON_Delete(json);fail_at=-1;assert(!live);}
 assert(failures>100);json=vehicle_assets_json();cJSON *photo=cJSON_GetArrayItem(cJSON_GetObjectItem(json,"photos"),0);cJSON_SetValuestring(photo,"invalid");assert(!vehicle_assets_parse(json));cJSON_Delete(json);assert(!live&&!locks);
 printf("PASS: profile/photo backup roundtrip, compact storage, failed commit preserves old data, restore and %u allocation failures without leaks\n",failures);
}
