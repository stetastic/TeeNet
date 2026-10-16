#include "charge_sessions.h"
#include "shell_search_schedule.h"
#include <assert.h>
#include <stdlib.h>
#include <stdio.h>
typedef struct {uint32_t period,from_date,to_date;bool custom;} session_filter_t;
static sessions_t sessions;
static unsigned locks,allocations,live;
static size_t allocated_bytes;
static bool fail,insert_during_allocation;
static bool session_filter_matches(const charge_session_t *row,const session_filter_t *filter){
    return filter->custom?row->start_date>=filter->from_date&&row->start_date<=filter->to_date:session_matches(row,filter->period);
}
#define LOCK() assert(locks++==0)
#define UNLOCK() assert(--locks==0)
static void append(uint32_t id,uint32_t date){
    session_store_t *store=&sessions.store;
    store->records[store->next_slot]=(charge_session_t){.id=id,.start_date=date,.grid_wh=1000,.cost_eur=.3f};
    store->active_slot=store->next_slot+1;store->next_slot=(store->next_slot+1)%CHARGE_SESSION_CAPACITY;
    if(store->count<CHARGE_SESSION_CAPACITY)store->count++;
}
static void *test_malloc(size_t size){
    assert(!locks);allocated_bytes=size;allocations++;
    if(insert_during_allocation){insert_during_allocation=false;append(99,20261012);}
    if(fail)return NULL;
    void *ptr=malloc(size);if(ptr)live++;return ptr;
}
static void test_free(void *ptr){assert(!locks);if(ptr){assert(live);live--;free(ptr);}}
#define malloc test_malloc
#define free test_free
#include "session_snapshot.inc"
#undef malloc
#undef free
int main(void){
    sessions_init(&sessions);session_filter_t filter={.period=202610};
    append(1,20260901);append(2,20261011);append(3,20261012);
    session_snapshot_t *copy=session_snapshot_create(&filter,0);
    assert(copy&&copy->retained==3&&copy->count==2&&copy->oldest_date==20260901&&copy->active_id==3);
    assert(session_snapshot_recent(copy,0)->id==3&&session_snapshot_recent(copy,1)->id==2&&!session_snapshot_recent(copy,2));
    assert(allocated_bytes==sizeof(*copy)+2*sizeof(charge_session_t)&&allocated_bytes<sizeof(session_store_t)/100);
    test_free(copy);
    copy=session_snapshot_create(NULL,20261012);assert(copy&&copy->count==1&&copy->records[0].id==3);test_free(copy);
    unsigned before=allocations;insert_during_allocation=true;copy=session_snapshot_create(&filter,0);
    assert(copy&&copy->count==3&&copy->records[0].id==99&&allocations==before+2&&live==1);test_free(copy);
    fail=true;assert(!session_snapshot_create(&filter,0)&&!live&&!locks);fail=false;
    sessions_init(&sessions);for(unsigned i=1;i<=CHARGE_SESSION_CAPACITY+17;i++)append(i,20261012);
    filter.period=202610;copy=session_snapshot_create(&filter,0);
    assert(copy&&copy->count==512&&copy->retained==512&&copy->records[0].id==529&&copy->records[511].id==18);test_free(copy);
    assert(shell_search_pause(0)==300000&&shell_search_pause(1)==900000&&shell_search_pause(2)==1800000&&shell_search_pause(999)==1800000);
    assert(!locks&&!live);
    puts("PASS: filtered session snapshot: chronological ring, metadata/active/costs retained, no 512-slot copy for small filters, concurrent new-session retry, OOM cleanup; discovery backoff 5/15/30 minutes");
}
