#include "ems_core.h"
#include "charge_counter.h"
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

typedef int esp_err_t;
typedef int nvs_handle_t;
enum {ESP_OK,ESP_ERR_NVS_NOT_FOUND,ESP_ERR_INVALID_SIZE,ESP_ERR_NO_MEM,ESP_FAIL,NVS_READONLY};
typedef struct {uint32_t version;energy_store_t totals;charge_counter_checkpoint_t counter;} meter_totals_t;
typedef struct {uint32_t version;energy_store_30_t totals;charge_counter_checkpoint_t counter;} meter_totals_30_t;
static energy_t energy;
static charge_counter_t charge_counter;
static bool locked;
#define LOCK() do{assert(!locked);locked=true;}while(0)
#define UNLOCK() do{assert(locked);locked=false;}while(0)
typedef struct {char key[20];unsigned char bytes[1800];size_t length;} slot_t;
static slot_t disk[34];
static unsigned fail_write,writes,handles;
static slot_t *lookup(const char *key){
    for(unsigned i=0;i<34;i++)if(!strcmp(disk[i].key,key))return &disk[i];
    return NULL;
}
static esp_err_t nvs_get_blob(nvs_handle_t nvs,const char *key,void *out,size_t *size){
    assert(nvs==1&&!locked);
    slot_t *s=lookup(key);
    if(!s)return ESP_ERR_NVS_NOT_FOUND;
    if(out){if(*size<s->length)return ESP_ERR_INVALID_SIZE;memcpy(out,s->bytes,s->length);}
    *size=s->length;
    return ESP_OK;
}
static esp_err_t nvs_set_blob(nvs_handle_t nvs,const char *key,const void *p,size_t size){
    assert(nvs==1&&!locked&&size<=sizeof(disk[0].bytes)&&strlen(key)<sizeof(disk[0].key));
    if(++writes==fail_write)return ESP_FAIL;
    slot_t *s=lookup(key);
    if(!s)s=lookup("");
    assert(s);
    strcpy(s->key,key);memcpy(s->bytes,p,size);s->length=size;
    return ESP_OK;
}
static esp_err_t nvs_commit(nvs_handle_t nvs){assert(nvs==1);return ESP_OK;}
static esp_err_t nvs_open_from_partition(const char *p,const char *ns,int mode,nvs_handle_t *h){
    assert(!strcmp(p,"stats")&&!strcmp(ns,"energy")&&mode==NVS_READONLY&&!locked);
    *h=1;handles++;return ESP_OK;
}
static void nvs_close(nvs_handle_t h){assert(h==1&&handles);handles--;}
#include "daily_storage.inc"

static void verify_days(uint32_t today,uint32_t first,unsigned expected,double first_wh){
    energy_day_reader_t reader;
    assert(energy_day_reader_open(&reader,today)==ESP_OK);
    unsigned n=0;
    for(;;){
        energy_day_t row;
        bool found=false;
        assert(energy_day_reader_next(&reader,&row,&found)==ESP_OK);
        if(!found)break;
        assert(row.date==first&&row.wh[0]==first_wh+n);
        first=history_next_date(first);n++;
    }
    energy_day_reader_close(&reader);
    assert(n==expected&&!handles&&!locked);
}

static void migration(unsigned count,bool metered,bool version1,unsigned fail){
    memset(disk,0,sizeof(disk));energy=(energy_t){0};charge_counter=(charge_counter_t){0};
    /* Construct historic records in their original layout and ring order. */
    unsigned char bytes[1800]={0};
    uint32_t version=1;
    const size_t prefix=metered?offsetof(meter_totals_t,totals):0;
    if(metered)memcpy(bytes,&version,sizeof(version));
    energy_store_t old={.version=version1?1:2,.total_wh={5000,10000}};
    size_t header=offsetof(energy_store_t,days);
    memcpy(bytes+prefix,&old,header);
    size_t stride=version1?offsetof(energy_day_t,charging_ms):sizeof(energy_day_t);
    uint32_t date=20260201,latest=date;
    for(unsigned i=0;i<count;i++){
        energy_day_t row={.date=date,.wh={i+1,i+2},.covered_ms={10000,20000},.charging_ms=9000};
        memcpy(bytes+prefix+header+(count-1-i)*stride,&row,stride);
        latest=date;date=history_next_date(date);
    }
    size_t len=prefix+header+stride*count;
    if(metered){
        charge_counter_checkpoint_t counter={.version=1,.anchored=true,.raw_wh=1234};
        memcpy(bytes+len,&counter,sizeof(counter));len+=sizeof(counter);
    }
    const char *key=metered?"meter_totals":"totals";
    writes=0;fail_write=0;assert(nvs_set_blob(1,key,bytes,len)==ESP_OK);
    writes=0;fail_write=fail;
    if(fail){
        assert(energy_days_load(1)==ESP_FAIL&&!lookup(DAILY_CHECKPOINT_KEY)&&lookup(key));
        assert(energy.store.total_wh[0]==0);
    }
    fail_write=0;
    assert(energy_days_load(1)==ESP_OK&&lookup(key));
    assert(energy.store.total_wh[0]==5000&&charge_counter.saved.raw_wh==(metered?1234:0));
    verify_days(0,20260201,count,1); /* No network time during boot. */
    verify_days(latest,20260201,count,1);
    writes=0;assert(energy_days_load(1)==ESP_OK&&!writes);
}

static void roll(void){
    memset(disk,0,sizeof(disk));energy=(energy_t){0};charge_counter=(charge_counter_t){0};
    uint32_t date=20240201;
    verify_days(0,0,0,0);
    for(unsigned i=1;i<=125;i++,date=history_next_date(date)){
        energy_add_metered(&energy,0,1000,10000,11000,date,0,i);
        energy.store.version=2;
        meter_totals_t next={.version=1,.totals=energy.store,.counter={.version=1,.raw_wh=i}};
        assert(energy_days_save(1,&next)==ESP_OK);
        writes=0;assert(energy_days_save(1,&next)==ESP_OK&&writes==1);
        energy=(energy_t){0};
        assert(energy_days_load(1)==ESP_OK&&energy.store.total_wh[0]==i*(i+1)/2.0);
        assert(charge_counter.saved.raw_wh==i);
        verify_days(date,i>=30?history_first_date(date):20240201,i>=30?30:i,i>=30?i-29:1);
    }
    /* A late delta for yesterday on a settled checkpoint updates that date once. */
    uint32_t latest=energy_latest_date(energy.store.days);
    energy_add_metered(&energy,1000,3000,1000,3000,latest,2000,2);
    meter_totals_t next={.version=1,.totals=energy.store,.counter={.version=1}};
    writes=0;fail_write=1;
    assert(energy_days_save(1,&next)==ESP_FAIL);
    assert(energy_days_load(1)==ESP_OK&&energy.store.total_wh[0]==125*126/2);
    fail_write=0;
    assert(energy_days_save(1,&next)==ESP_OK);
    assert(energy_days_load(1)==ESP_OK&&energy.store.total_wh[0]==125*126/2+2);
    assert(energy_find_date(energy.store.days,latest)->wh[0]==126);
    /* The oldest day has left RAM but remains visible before the flash save. */
    uint32_t next_date=history_next_date(latest);
    energy_add_metered(&energy,0,1000,10000,11000,next_date,0,126);
    next.totals=energy.store;
    uint32_t evicted=history_first_date(latest);
    for(unsigned i=0;i<27;i++)evicted=history_next_date(evicted);
    assert(!energy_find_date(energy.store.days,evicted));
    energy_day_reader_t reader;
    assert(energy_day_reader_open(&reader,next_date)==ESP_OK);
    reader.next=evicted;
    energy_day_t row;bool found=false;
    assert(energy_day_reader_next(&reader,&row,&found)==ESP_OK&&found&&row.date==evicted);
    energy_day_reader_close(&reader);
    writes=0;fail_write=2;
    assert(energy_days_save(1,&next)==ESP_FAIL);
    assert(energy_days_load(1)==ESP_OK&&energy.store.total_wh[0]==125*126/2+2);
    fail_write=0;assert(energy_days_save(1,&next)==ESP_OK);
}

int main(void){
    assert(history_first_date(20260301)==20260131);
    assert(history_first_date(20240301)==20240201);
    assert(history_first_date(20260101)==20251203);
    assert(history_first_date(20260229)==0);
    for(unsigned i=1;i<=31;i++)migration(30,true,false,i);
    migration(7,true,false,0);migration(7,false,false,0);migration(30,false,true,0);
    roll();
    printf("PASS: 30-day calendar, 7/30-day migration, 31 interrupted migrations, atomic meter anchor, late delta, 125 daily reboots. Working days %zu B, reader %zu B, checkpoint %zu B.\n",
           sizeof(energy.store.days),sizeof(energy_day_reader_t),sizeof(meter_totals_t));
    return 0;
}
