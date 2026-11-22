#include "ems_core.h"
#include "charge_sessions.h"
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
typedef int nvs_handle_t;typedef int esp_err_t;
enum {ESP_OK=0,ESP_ERR_NVS_NOT_FOUND=1,ESP_ERR_INVALID_STATE=2,ESP_ERR_NO_MEM=3,NVS_READONLY=0};
static sessions_t sessions;static settings_t settings;
static struct {char key[16];unsigned char data[1024];size_t size;} entries[70];
static unsigned writes;static int fail_after=-1;
static int find(const char *key){for(int i=0;i<70;i++)if(entries[i].size&&!strcmp(entries[i].key,key))return i;return -1;}
static int nvs_get_blob(int h,const char *key,void *out,size_t *n){assert(h==7);int i=find(key);if(i<0)return ESP_ERR_NVS_NOT_FOUND;if(!out){*n=entries[i].size;return 0;}if(*n<entries[i].size)return 2;memcpy(out,entries[i].data,entries[i].size);*n=entries[i].size;return 0;}
static int nvs_set_blob(int h,const char *key,const void *value,size_t n){assert(h==7&&n<=1024);if(fail_after==0)return 2;if(fail_after>0)fail_after--;writes++;int i=find(key);if(i<0)for(int j=0;j<70;j++)if(!entries[j].size){i=j;break;}assert(i>=0);strcpy(entries[i].key,key);entries[i].size=n;memcpy(entries[i].data,value,n);return 0;}
static int nvs_commit(int h){assert(h==7);return 0;}
static int nvs_erase_key(int h,const char *key){assert(h==7);int i=find(key);if(i>=0)entries[i].size=0;return 0;}
#include "session_storage.inc"
static int status_snapshot_lock=1;
static bool stats_storage_ok=true;
#define LOCK() ((void)0)
#define UNLOCK() ((void)0)
#define pdTRUE 1
static int ticks(int n){return n;}
static int xSemaphoreTake(int l,int t){assert(l==1&&t==1000);return 1;}
static void xSemaphoreGive(int l){assert(l==1);}
static int nvs_open_from_partition(const char*p,const char*n,int mode,int*h){assert(!strcmp(p,"stats")&&!strcmp(n,"energy")&&!mode);*h=7;return 0;}
static void nvs_close(int h){assert(h==7);}
#include "session_reader.inc"

static esp_err_t save(void){
    struct {uint32_t count,next_slot,next_id,active_slot;charge_session_t rows[CHARGE_SESSION_CAPACITY];} storage;
    session_checkpoint_t *cp=(session_checkpoint_t *)&storage;
    assert(session_checkpoint_copy(cp,CHARGE_SESSION_CAPACITY,&sessions.store));
    return session_chunks_save_checkpoint(7,cp);
}
static void reset(void){
    memset(entries,0,sizeof(entries));sessions_init(&sessions);session_disk_gap_id=0;
    assert(session_chunks_load(7)==0);writes=0;
}
static void append(void){
    session_store_t *s=&sessions.store;
    s->records[s->next_slot]=(charge_session_t){.id=s->next_id,.grid_wh=1500,.start_date=20251001,.cost_eur=.3,.grid_cost_eur=.3};
    s->active_slot=s->next_slot+1;s->next_slot=(s->next_slot+1)%CHARGE_SESSION_CAPACITY;
    if(s->count<CHARGE_SESSION_CAPACITY)s->count++;
    s->next_id++;if(!s->next_id)s->next_id=1;
}
static void verify(unsigned first,unsigned last){
    session_reader_t r;assert(session_reader_open(&r)&&r.count==last-first+1);
    for(unsigned i=0;i<r.count;i++)assert(session_reader_at(&r,i)->id==last-i);
    for(unsigned i=r.count;i>0;i--)assert(session_reader_at(&r,i-1)->id==last-i+1);
    session_reader_close(&r);
}
static void old_disk(unsigned version,unsigned count,unsigned next,unsigned id){
    unsigned cap=version==2?50:512;
    session_disk_head_t h={.version=version,.count=count,.next_slot=next,.next_id=id,.active_slot=next?next:cap};
    for(unsigned b=0;b<(cap+15)/16;b++){
        charge_session_t rows[16]={0};
        for(unsigned j=0;j<16;j++){
            unsigned slot=b*16+j;if(slot>=cap)break;
            unsigned age=(next+cap-1-slot)%cap;
            if(age<count)rows[j]=(charge_session_t){.id=id-1-age,.grid_wh=1500};
        }
        uint32_t bit=UINT32_C(1)<<b;
        char key[12];session_block_key(key,b,(b&1)!=0);
        h.valid|=bit;if(b&1)h.banks|=bit;
        assert(nvs_set_blob(7,key,rows,sizeof(rows))==0);
    }
    assert(nvs_set_blob(7,"sess_head",&h,sizeof(h))==0);
}

/* Minimal NVS image builder; a single logical blob can span several 4 KiB
   pages, and a 48-byte record may straddle a non-row-aligned chunk boundary. */
static uint32_t esp_rom_crc32_le(uint32_t seed,const uint8_t *p,size_t n){
    uint32_t crc=~seed;
    for(size_t i=0;i<n;i++){crc^=p[i];for(int k=0;k<8;k++)crc=(crc>>1)^((crc&1)?UINT32_C(0xedb88320):0);}
    return ~crc;
}
#include "session_monolith_import.inc"
static uint8_t image[4096*16];
static void put32(uint8_t *p,uint32_t v){memcpy(p,&v,4);}
static uint8_t *item(unsigned page,unsigned idx,unsigned ns,unsigned type,unsigned span,unsigned chunk,const char *key){
    uint8_t *p=image+4096*page;
    put32(p,0xfffffffc);
    for(unsigned j=idx;j<idx+span;j++)p[32+j/4]&=~(1u<<((j%4)*2));
    uint8_t *it=p+64+32*idx;it[0]=ns;it[1]=type;it[2]=span;it[3]=chunk;memset(it+8,0,16);strcpy((char*)it+8,key);return it;
}
static void seal(uint8_t *it){uint32_t crc=esp_rom_crc32_le(UINT32_MAX,it,4);put32(it+4,esp_rom_crc32_le(crc,it+8,24));}
static void monolith(void){
    static struct {uint32_t version,count,next_slot,next_id,active_slot;session_v1_t rows[512];} raw;
    raw.version=1;raw.count=512;raw.next_slot=17;raw.next_id=513;raw.active_slot=17;
    for(unsigned i=0;i<512;i++)raw.rows[(17+i)%512]=(session_v1_t){.id=1+i,.grid_wh=2000,.cost_eur=.5};
    memset(image,0xff,sizeof(image));
    uint8_t *it=item(0,0,0,1,1,255,"energy");it[24]=1;seal(it);
    size_t pos=0;unsigned part=0;
    while(pos<sizeof(raw)){
        size_t n=sizeof(raw)-pos;if(n>3745)n=3745;
        it=item(part+1,0,1,0x42,1+(n+31)/32,128+part,"sessions");
        uint16_t small=n;memcpy(it+24,&small,2);memcpy(it+32,(uint8_t*)&raw+pos,n);
        put32(it+28,esp_rom_crc32_le(UINT32_MAX,it+32,n));seal(it);
        pos+=n;part++;
    }
    it=item(0,1,1,0x48,1,255,"sessions");put32(it+24,sizeof(raw));it[28]=part;it[29]=128;seal(it);
    fail_after=3;assert(session_import_monolith(7,image,sizeof(image),sizeof(raw))!=0);fail_after=-1;
    assert(find("sess_head")==-1);
    image[4096+96+39]^=1;
    assert(session_import_monolith(7,image,sizeof(image),sizeof(raw))!=0&&find("sess_head")==-1);
    image[4096+96+39]^=1;
    assert(session_import_monolith(7,image,sizeof(image),sizeof(raw))==0&&session_chunks_load(7)==1);
    verify(1,512);
    const charge_session_t *last=sessions_recent(&sessions.store,0);
    assert(last->flags&SESSION_COST_SPLIT_ESTIMATED);assert(last->flags&SESSION_INTERRUPTED);
    assert(last->grid_cost_eur==.5f);
}
int main(void){
    settings_defaults(&settings);reset();verify(1,0);
    for(unsigned i=0;i<548;i++){append();assert(save()==0);}
    verify(37,548);
    assert(sizeof(session_store_t)<256&&sizeof(session_reader_t)<1100);
    writes=0;assert(save()==0&&writes==0);
    session_reader_t older;assert(session_reader_open(&older));
    sessions.store.records[sessions.store.active_slot-1].grid_wh=2300;
    fail_after=1;assert(save()!=0);fail_after=-1;
    assert(session_chunks_load(7)==1);
    assert(sessions_recent(&sessions.store,0)->grid_wh==1500&&sessions_recent(&sessions.store,0)->flags&SESSION_INTERRUPTED);
    assert(save()==0);assert(!session_reader_at(&older,500));session_reader_close(&older);
    /* More sessions than RAM slots while storage is offline. Recover without
       forgetting the prior 512 or pretending the missing records exist. */
    for(unsigned i=0;i<9;i++)append();
    assert(save()==0);assert(save()==0);
    assert(session_chunks_load(7)==1);
    session_reader_t r;assert(session_reader_open(&r)&&r.count==512);
    assert(session_reader_at(&r,0)->id==557&&session_reader_at(&r,3)->id==554&&session_reader_at(&r,4)->id==548);
    assert(session_reader_at(&r,3)->flags&SESSION_ARCHIVE_GAP);
    session_reader_close(&r);
    sessions_init(&sessions);assert(save()==0&&session_chunks_load(7)==1);verify(1,0);
    reset();old_disk(1,512,3,700);assert(session_chunks_load(7)==1);verify(188,699);
    assert(save()==0&&session_disk.version==1);verify(188,699);
    reset();old_disk(2,50,7,81);
    fail_after=2;assert(session_chunks_load(7)<0);fail_after=-1;
    assert(session_chunks_load(7)==1);verify(31,80);append();assert(save()==0);verify(31,81);
    reset();monolith();
    printf("PASS: 512-session rollover, wrapped 50/512 migration, CRC-checked monolith, write interruption, storage recovery; RAM store %zu bytes, reader %zu bytes irrespective of rows\n",sizeof(session_store_t),sizeof(session_reader_t));
}
