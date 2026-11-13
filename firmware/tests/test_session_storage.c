#include "ems_core.h"
#include "charge_sessions.h"
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
typedef int nvs_handle_t;typedef int esp_err_t;
enum {ESP_OK=0,ESP_ERR_NVS_NOT_FOUND=1};
static sessions_t sessions;static settings_t settings;
static struct {char key[16];unsigned char data[1024];size_t size;} entries[70];
static unsigned writes;static int fail_after=-1;
static int find(const char *key){for(int i=0;i<70;i++)if(entries[i].size&&!strcmp(entries[i].key,key))return i;return -1;}
static int nvs_get_blob(int h,const char *key,void *out,size_t *n){assert(h==7);int i=find(key);if(i<0)return ESP_ERR_NVS_NOT_FOUND;if(!out){*n=entries[i].size;return 0;}if(*n<entries[i].size)return 2;memcpy(out,entries[i].data,entries[i].size);*n=entries[i].size;return 0;}
static int nvs_set_blob(int h,const char *key,const void *value,size_t n){assert(h==7&&n<=1024);if(fail_after==0)return 2;if(fail_after>0)fail_after--;writes++;int i=find(key);if(i<0)for(int j=0;j<70;j++)if(!entries[j].size){i=j;break;}assert(i>=0);strcpy(entries[i].key,key);entries[i].size=n;memcpy(entries[i].data,value,n);return 0;}
static int nvs_commit(int h){assert(h==7);return 0;}
static int nvs_erase_key(int h,const char *key){assert(h==7);int i=find(key);if(i>=0)entries[i].size=0;return 0;}
#include "session_storage.inc"
int main(void){
 settings_defaults(&settings);sessions_init(&sessions);sessions.store.count=CHARGE_SESSION_CAPACITY;sessions.store.next_slot=0;sessions.store.next_id=513;
 for(unsigned i=0;i<CHARGE_SESSION_CAPACITY;i++)sessions.store.records[i]=(charge_session_t){.id=i+1,.grid_wh=2000,.cost_eur=.5,.grid_cost_eur=.5,.start_date=20261029};
 assert(session_chunks_save(7,&sessions.store)==0&&writes==33);sessions_init(&sessions);assert(session_chunks_load(7)==1&&sessions.store.count==512&&sessions.store.records[511].id==512);
 session_checkpoint_t *full=malloc(sizeof(*full)+CHARGE_SESSION_CAPACITY*sizeof(charge_session_t));assert(full&&session_checkpoint_copy(full,CHARGE_SESSION_CAPACITY,&sessions.store));writes=0;assert(session_chunks_save_checkpoint(7,full)==0&&writes==1);free(full);
 writes=0;sessions.store.records[0].grid_wh=3000;sessions.store.records[0].cost_eur=sessions.store.records[0].grid_cost_eur=.75;
 assert(session_chunks_save(7,&sessions.store)==0&&writes==2); /* One block plus the atomic bank map, not 512 rows. */
 sessions.store.records[0].grid_wh=4000;sessions.store.records[0].cost_eur=sessions.store.records[0].grid_cost_eur=1;
 fail_after=1;assert(session_chunks_save(7,&sessions.store)!=0);fail_after=-1;sessions_init(&sessions);assert(session_chunks_load(7)==1&&sessions.store.records[0].grid_wh==3000);
 assert(sessions.store.records[511].id==512);sessions_init(&sessions);assert(session_chunks_save(7,&sessions.store)==0);assert(session_chunks_load(7)==1&&!sessions.store.count);
 /* A wrapped ring with only three retained rows needs no 512-row RAM copy. */
 sessions.store.count=3;sessions.store.next_slot=1;sessions.store.next_id=104;sessions.store.active_slot=1;
 for(unsigned i=0;i<3;i++)sessions.store.records[(510+i)%512]=(charge_session_t){.id=101+i,.grid_wh=1500+i};
 size_t compact_bytes=sizeof(session_checkpoint_t)+3*sizeof(charge_session_t);
 session_checkpoint_t *copy=malloc(compact_bytes);assert(copy);
 assert(!session_checkpoint_copy(copy,2,&sessions.store));assert(session_checkpoint_copy(copy,3,&sessions.store));
 assert(copy->records[0].id==101&&copy->records[2].id==103);
 assert(!session_checkpoint_at(copy,1)&&session_checkpoint_at(copy,0)->id==103);
 assert(session_chunks_save_checkpoint(7,copy)==0);sessions_init(&sessions);assert(session_chunks_load(7)==1);
 assert(sessions.store.count==3&&sessions.store.next_slot==1&&!sessions.store.active_slot&&sessions.store.records[510].id==101&&sessions.store.records[0].id==103&&(sessions.store.records[0].flags&SESSION_INTERRUPTED));
 copy->records[2].grid_wh=9000;fail_after=1;assert(session_chunks_save_checkpoint(7,copy)!=0);fail_after=-1;
 sessions_init(&sessions);assert(session_chunks_load(7)==1&&sessions.store.records[0].grid_wh==1502);free(copy);
 sessions_init(&sessions);assert(session_chunks_save(7,&sessions.store)==0);
 unsigned bytes=0;for(unsigned i=0;i<70;i++)bytes+=(unsigned)entries[i].size;assert(bytes==sizeof(session_disk_head_t));
 printf("PASS: 512 sessions, incremental block writes, interrupted checkpoint retains old bank; compact wrapped 3-row checkpoint uses %zu bytes instead of %zu; complete restart and reset cleanup\n",compact_bytes,sizeof(session_store_t));
}
