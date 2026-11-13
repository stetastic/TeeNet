#include "ems_core.h"
#include "charge_counter.h"
#include "session_checkpoint.h"
#include <assert.h>
#include <inttypes.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
typedef int esp_err_t;typedef int nvs_handle_t;
enum{ESP_OK=0,ESP_FAIL=1,NVS_READWRITE=1};
#define pdTRUE 1
#define ticks(x) (x)
#define ESP_LOGW(...) ((void)0)
static int state_locks,status_lock,flash_lock;static const int status_snapshot_lock=1,maintenance_flash_lock=2;
static bool busy,stats_storage_ok=true,storage_error;static int64_t last_saved_ms;
static energy_t energy;static sessions_t sessions;static charge_counter_t charge_counter;
typedef struct{uint32_t version;energy_store_t totals;charge_counter_checkpoint_t counter;}meter_totals_t;
static double energy_reset_wh[EMS_SOURCES];static uint32_t energy_reset_date;
static size_t used,peak,allocation_limit=8192,calls,fail_allocation;static int fail_write,write_calls;
typedef union{max_align_t alignment;size_t bytes;}allocation_t;
static unsigned saved_count,saved_first,saved_last;static meter_totals_t saved_meter;
static void *limited_malloc(size_t n){
 if(++calls==fail_allocation||n>allocation_limit-used)return NULL;
 allocation_t *p=malloc(sizeof(*p)+n);assert(p);p->bytes=n;used+=n;if(used>peak)peak=used;return p+1;
}
static void limited_free(void *v){if(v){allocation_t *p=(allocation_t *)v-1;assert(used>=p->bytes);used-=p->bytes;free(p);}}
#define LOCK() assert(state_locks++==0)
#define UNLOCK() assert(--state_locks==0)
static int xSemaphoreTake(int id,int timeout){(void)timeout;assert(!state_locks);if(id==1){if(busy)return 0;assert(!status_lock);status_lock=1;}else{assert(id==2&&!flash_lock);flash_lock=1;}return pdTRUE;}
static void xSemaphoreGive(int id){assert(!state_locks);if(id==1){assert(status_lock);status_lock=0;}else{assert(id==2&&flash_lock);flash_lock=0;}}
static int64_t now_ms(void){return 12000;}
static int write_result(void){return ++write_calls==fail_write?ESP_FAIL:ESP_OK;}
static int nvs_open_from_partition(const char*p,const char*n,int mode,int*h){assert(!strcmp(p,"stats")&&!strcmp(n,"energy")&&mode==NVS_READWRITE&&status_lock);*h=7;return ESP_OK;}
static int nvs_set_blob(int h,const char*k,const void*p,size_t n){assert(h==7);if(!strcmp(k,"meter_totals")){assert(n==sizeof(saved_meter));memcpy(&saved_meter,p,n);}return write_result();}
static int nvs_set_u32(int h,const char*k,uint32_t v){assert(h==7&&!strcmp(k,"reset_date")&&v==energy_reset_date);return write_result();}
static int nvs_commit(int h){assert(h==7);return write_result();}
static void nvs_close(int h){assert(h==7);}
static int session_chunks_save_checkpoint(int h,const session_checkpoint_t*c){assert(h==7);saved_count=c->count;saved_first=c->count?c->records[0].id:0;saved_last=c->count?c->records[c->count-1].id:0;return write_result();}
#define malloc limited_malloc
#define free limited_free
#include "energy_save_actual.inc"
#undef malloc
#undef free

typedef struct{int status;size_t bytes;char message[140];}httpd_req_t;
static settings_t settings;static bool restarting,ota_in_progress,backup_in_progress;
static struct{bool running;}github_update;
static int64_t actual_at=10000;static float actual_a[3];
#define EMS_VERSION "1.23"
static bool authorized(httpd_req_t*r){(void)r;return true;}
static void httpd_resp_set_status(httpd_req_t*r,const char*s){r->status=atoi(s);}
static int httpd_resp_sendstr(httpd_req_t*r,const char*s){snprintf(r->message,sizeof(r->message),"%s",s);return ESP_OK;}
static int httpd_resp_set_type(httpd_req_t*r,const char*s){(void)r;(void)s;return ESP_OK;}
static int httpd_resp_set_hdr(httpd_req_t*r,const char*k,const char*v){(void)r;(void)k;(void)v;return ESP_OK;}
static unsigned chunks,fail_chunk;
static int httpd_resp_send_chunk(httpd_req_t*r,const char*b,size_t n){(void)b;if(++chunks==fail_chunk)return ESP_FAIL;r->bytes+=n;return ESP_OK;}
static void reset_sequence_clear(void){}
static bool save_plan(void){return true;}
static bool save_control_memory(bool force){assert(force);return true;}
static void save_diagnostics(bool force){assert(force);}
static int esp_flash_get_size(void*x,uint32_t*n){assert(!x);*n=4*1024*1024;return ESP_OK;}
static int esp_flash_read(void*x,void*b,uint32_t at,uint32_t n){assert(!x&&at<4*1024*1024&&n<=4096);memset(b,0,n);return ESP_OK;}
static void vTaskDelay(int ms){(void)ms;}
#define malloc limited_malloc
#define free limited_free
#include "system_backup.inc"
#undef malloc
#undef free
static void clean(void){assert(!used&&!status_lock&&!flash_lock&&!state_locks&&!backup_in_progress&&!ota_in_progress);}
int main(void){
 expert_values_t defaults;expert_defaults(&defaults);assert(expert_activate(&defaults));
 energy.store.version=2;energy.store.total_wh[0]=1234.5;charge_counter.saved.raw_wh=2345.6;energy_reset_date=20261105;
 sessions.store=(session_store_t){.version=2,.count=3,.next_slot=3,.next_id=4};
 for(unsigned i=0;i<3;i++)sessions.store.records[i]=(charge_session_t){.id=i+1,.grid_wh=1500};
 assert(save_energy());clean();assert(saved_count==3&&saved_first==1&&saved_last==3&&last_saved_ms==12000&&!storage_error);
 assert(saved_meter.totals.total_wh[0]==1234.5&&saved_meter.counter.raw_wh==2345.6&&saved_meter.counter.version==1);
 size_t small_peak=peak;assert(small_peak<8192&&sizeof(session_store_t)>8192);
 busy=true;write_calls=0;assert(!save_energy()&&!storage_error&&!write_calls);busy=false;clean();
 for(unsigned i=1;i<=2;i++){calls=0;fail_allocation=i;write_calls=0;assert(!save_energy()&&storage_error&&!write_calls);clean();fail_allocation=0;assert(save_energy()&&!storage_error);clean();}
 for(int i=1;i<=6;i++){write_calls=0;fail_write=i;assert(!save_energy()&&storage_error);clean();fail_write=0;assert(save_energy()&&!storage_error);clean();}
 sessions.store.count=512;sessions.store.next_slot=0;sessions.store.next_id=513;for(unsigned i=0;i<512;i++)sessions.store.records[i].id=i+1;
 assert(!save_energy()&&storage_error);clean();allocation_limit=64000;assert(save_energy()&&!storage_error&&saved_count==512&&saved_first==1&&saved_last==512);clean();
 sessions.store.count=3;sessions.store.next_slot=3;allocation_limit=8192;
 httpd_req_t r={.status=200};busy=true;assert(system_backup_handler(&r)==ESP_OK&&r.status==503&&strstr(r.message,"Verbrauchsdaten")&&!r.bytes);busy=false;clean();
 r=(httpd_req_t){.status=200};chunks=0;assert(system_backup_handler(&r)==ESP_OK&&r.status==200&&r.bytes==4*1024*1024);clean();
 r=(httpd_req_t){.status=200};chunks=0;fail_chunk=2;assert(system_backup_handler(&r)==ESP_FAIL&&r.bytes==4096);fail_chunk=0;clean();
 settings.enabled=true;r=(httpd_req_t){.status=200};assert(system_backup_handler(&r)==ESP_OK&&r.status==409&&!r.bytes);clean();
 printf("PASS: actual energy save succeeds with 8KB budget (%zu-byte peak); allocation/write faults release all memory and recover; 512-row capacity; busy backup gives 503, complete/interrupted streams and charge guard\n",small_peak);
}
