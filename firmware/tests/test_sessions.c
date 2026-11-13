#include "charge_sessions.h"
#include <assert.h>
#include <math.h>
#include <stdio.h>
#include <string.h>

static sessions_t s,restored;
static void near(double a,double b){assert(fabs(a-b)<.002);}
static void sample(int64_t t,double w,double grid){sessions_step(&s,t,1790150000000LL+t,20260923,w,t+5000,grid,t+10000,.25f,.08f);}
static const charge_session_t *current(void){return s.pending_active?&s.pending_session:sessions_recent(&s.store,0);}
int main(void){
    sessions_init(&s);sample(1000,0,2000);sample(2000,600,2000);assert(s.store.count==0);
    sessions_init(&s);sample(1000,5000,2000);
    for(int i=1;i<=3600;i++)sample(1000+(int64_t)i*1000,5000,2000);
    const charge_session_t *r=sessions_recent(&s.store,0);
    near(r->grid_wh,2000);near(r->solar_wh,3000);near(r->unknown_wh,0);near(r->cost_eur,.74);assert(r->charging_ms==3600000);
    sample(3601000,0,0);for(int i=1;i<=299;i++)sample(3601000+(int64_t)i*1000,0,0);assert(s.store.active_slot);
    sample(3901000,0,0);assert(!s.store.active_slot&&s.checkpoint);assert(s.store.count==1);
    sample(3902000,5000,-2000);sample(3903000,5000,-2000);r=current();near(r->grid_wh,0);near(r->solar_wh,5000.0/3600);assert(s.store.count==1&&s.pending_active);
    assert(sessions_restore(&restored,&s.store,sizeof(s.store),.25f,.08f));assert(!restored.store.active_slot&&restored.store.count==1);

    sessions_init(&s);
    sessions_step(&s,1000,1790150001000LL,20260923,5000,6000,2000,1500,.25f,.08f);
    sessions_step(&s,2000,1790150002000LL,20260923,5000,7000,2000,12000,.25f,.08f);
    r=current();near(r->grid_wh,(2000*.5+5000*.5)/3600);near(r->solar_wh,3000*.5/3600);near(r->unknown_wh,0);
    sessions_init(&s);sample(1000,5000,9000);sample(2000,5000,9000);r=current();near(r->grid_wh,5000.0/3600);near(r->solar_wh,0);

    /* Tariff changes are prospective; short pauses stay in the same session. */
    sessions_init(&s);sample(1000,5000,2000);
    for(int i=1;i<=3600;i++)sample(1000+(int64_t)i*1000,5000,2000);
    for(int i=3600;i<=7200;i++)sessions_step(&s,1000+(int64_t)i*1000,1790150001000LL+(int64_t)i*1000,20260923,5000,6000+(int64_t)i*1000,2000,11000+(int64_t)i*1000,.50f,.10f);
    r=sessions_recent(&s.store,0);near(r->cost_eur,.74+1.30);
    near(r->grid_cost_eur,1.50);near(r->solar_cost_eur,.54);
    assert(sessions_restore(&restored,&s.store,sizeof(s.store),.99f,.99f));
    const charge_session_t *historic=sessions_recent(&restored.store,0);
    near(historic->grid_cost_eur,1.50);near(historic->solar_cost_eur,.54);near(historic->cost_eur,2.04);
    sample(7201000,0,0);sample(7211000,0,0);sample(7221000,5000,0);assert(s.store.count==1);
    /* Stale wallbox readings never add energy during a network/meter gap. */
    double before=r->grid_wh+r->solar_wh+r->unknown_wh;
    sample(7231000,5000,0);near(r->grid_wh+r->solar_wh+r->unknown_wh,before);

    sessions_init(&s);int64_t t=1000;
    for(unsigned i=0;i<CHARGE_SESSION_CAPACITY+3;i++){sample(t,900000,0);sample(t+1000,900000,0);sample(t+2000,900000,0);sample(t+3000,900000,0);sample(t+4000,0,0);sample(t+4000+SESSION_PAUSE_MS,0,0);t+=SESSION_PAUSE_MS+5000;}
    assert(s.store.count==CHARGE_SESSION_CAPACITY);assert(sessions_recent(&s.store,0)->id==CHARGE_SESSION_CAPACITY+3);assert(sessions_recent(&s.store,CHARGE_SESSION_CAPACITY-1)->id==4);
    assert(!sessions_recent(&s.store,CHARGE_SESSION_CAPACITY));
    uint32_t period;assert(session_period_parse("2026-09",&period)&&period==202609);assert(session_matches(sessions_recent(&s.store,0),period));
    assert(session_period_parse("2026",&period)&&period==2026);assert(session_matches(sessions_recent(&s.store,0),period));
    assert(!session_period_parse("2026-13",&period));assert(!session_period_parse("2026-9",&period));assert(!session_period_parse("2026x",&period));
    s.store.records[0].grid_wh=NAN;assert(!sessions_restore(&restored,&s.store,sizeof(s.store),.25f,.08f));
    typedef struct {uint32_t id,start_epoch,end_epoch,start_date,charging_ms;float grid_wh,solar_wh,unknown_wh,cost_eur;uint32_t flags;} old_row_t;
    static struct {uint32_t version,count,next_slot,next_id,active_slot;old_row_t records[CHARGE_SESSION_CAPACITY];} old;
    old.version=1;old.count=1;old.next_slot=1;old.next_id=2;
    old.records[0]=(old_row_t){.id=1,.start_date=20260923,.grid_wh=2000,.solar_wh=3000,.cost_eur=.74f};
    assert(sessions_restore(&restored,&old,sizeof(old),.25f,.08f));
    historic=sessions_recent(&restored.store,0);near(historic->cost_eur,.74);near(historic->grid_cost_eur,.5);near(historic->solar_cost_eur,.24);
    assert(historic->flags&SESSION_COST_SPLIT_ESTIMATED);assert(restored.checkpoint);
    s=restored;assert(sessions_restore(&restored,&s.store,sizeof(s.store),.8f,.5f));
    historic=sessions_recent(&restored.store,0);near(historic->cost_eur,.74);near(historic->grid_cost_eur,.5);
    puts("PASS: session boundaries, energy split, tariffs, expiry, restart, retention and monthly/yearly filters");
}
