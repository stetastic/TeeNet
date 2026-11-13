#include "charge_sessions.h"
#include <string.h>
#include <math.h>
#include <stdlib.h>
#include <limits.h>

static bool bounded(double value,double hi){return isfinite(value)&&value>=0&&value<=hi;}
static charge_session_t *active(sessions_t *s){return s->pending_active?&s->pending_session:s->store.active_slot?&s->store.records[s->store.active_slot-1]:NULL;}
bool session_recorded(const charge_session_t *r){return r&&r->id&&(double)r->grid_wh+r->solar_wh+r->unknown_wh>=SESSION_MIN_WH-.001;}
static void promote(sessions_t *s){
    if(!s->pending_active||s->grid_wh+s->solar_wh+s->unknown_wh<SESSION_MIN_WH-1e-6)return;
    unsigned slot=s->store.next_slot;s->pending_session.id=s->store.next_id++;if(!s->store.next_id)s->store.next_id=1;
    s->store.records[slot]=s->pending_session;s->store.active_slot=slot+1;s->store.next_slot=(slot+1)%CHARGE_SESSION_CAPACITY;
    if(s->store.count<CHARGE_SESSION_CAPACITY)s->store.count++;
    s->pending_active=false;s->checkpoint=true;
}
typedef struct {
    uint32_t id,start_epoch,end_epoch,start_date,charging_ms;
    float grid_wh,solar_wh,unknown_wh,cost_eur;
    uint32_t flags;
} session_v1_t;
typedef struct {
    uint32_t version,count,next_slot,next_id,active_slot;
    session_v1_t records[CHARGE_SESSION_CAPACITY];
} session_store_v1_t;
void sessions_init(sessions_t *s){memset(s,0,sizeof(*s));s->store.version=2;s->store.next_id=1;}
bool sessions_restore(sessions_t *s,const void *blob,size_t size,float grid_price,float solar_price){
    if(!blob || (size!=sizeof(session_store_t) && size!=sizeof(session_store_v1_t)))return false;
    const session_store_t *in=blob;
    bool legacy=size==sizeof(session_store_v1_t);
    if(in->version!=(legacy?1u:2u)||in->count>CHARGE_SESSION_CAPACITY||in->next_slot>=CHARGE_SESSION_CAPACITY||in->active_slot>CHARGE_SESSION_CAPACITY||!in->next_id)return false;
    for(unsigned i=0;i<CHARGE_SESSION_CAPACITY;i++){
        charge_session_t row={0};
        if(legacy)memcpy(&row,&((const session_store_v1_t *)blob)->records[i],sizeof(session_v1_t));
        else row=in->records[i];
        if(!bounded(row.grid_wh,1e9)||!bounded(row.solar_wh,1e9)||!bounded(row.unknown_wh,1e9)||!bounded(row.cost_eur,1e8))return false;
        if(!legacy && (!bounded(row.grid_cost_eur,1e8)||!bounded(row.solar_cost_eur,1e8)||
            fabs((double)row.grid_cost_eur+row.solar_cost_eur-row.cost_eur)>fmax(.01,row.cost_eur*1e-5)))return false;
    }
    sessions_init(s);
    if(!legacy)s->store=*in;
    else {
        const session_store_v1_t *old=blob;
        s->store.count=old->count;s->store.next_slot=old->next_slot;s->store.next_id=old->next_id;s->store.active_slot=old->active_slot;
        grid_price=bounded(grid_price,10)?grid_price:.25f;solar_price=bounded(solar_price,10)?solar_price:.08f;
        for(unsigned i=0;i<CHARGE_SESSION_CAPACITY;i++){
            charge_session_t *r=&s->store.records[i];memcpy(r,&old->records[i],sizeof(session_v1_t));
            /* Old firmware stored the total only. Preserve that total and
               freeze the estimated source split once, never on page render. */
            double weight=(double)r->grid_wh*grid_price+(double)r->solar_wh*solar_price;
            double share=weight>0?(double)r->grid_wh*grid_price/weight:
                r->grid_wh+r->solar_wh>0?r->grid_wh/(double)(r->grid_wh+r->solar_wh):1;
            r->grid_cost_eur=(float)(r->cost_eur*share+r->unknown_wh*grid_price/1000);
            r->solar_cost_eur=(float)(r->cost_eur*(1-share));
            r->cost_eur=r->grid_cost_eur+r->solar_cost_eur;
            r->grid_wh+=r->unknown_wh;r->unknown_wh=0;
            if(r->id)r->flags|=SESSION_COST_SPLIT_ESTIMATED;
        }
        s->checkpoint=true;
    }
    if(active(s)){active(s)->flags|=SESSION_INTERRUPTED;s->store.active_slot=0;s->checkpoint=true;}
    return true;
}
const charge_session_t *sessions_recent(const session_store_t *s,unsigned index){
    if(index>=s->count)return NULL;
    return &s->records[(s->next_slot+CHARGE_SESSION_CAPACITY-1-index)%CHARGE_SESSION_CAPACITY];
}
bool session_matches(const charge_session_t *s,uint32_t period){return period<10000?s->start_date/10000==period:s->start_date/100==period;}
bool session_period_parse(const char *text,uint32_t *period){
    if(!text||!period)return false;
    size_t n=strlen(text);if(n!=4&&n!=7)return false;
    unsigned year=0,month=0;
    for(unsigned i=0;i<4;i++){if(text[i]<'0'||text[i]>'9')return false;year=year*10+(unsigned)(text[i]-'0');}
    if(year<2024||year>2100)return false;
    if(n==7){if(text[4]!='-'||text[5]<'0'||text[5]>'9'||text[6]<'0'||text[6]>'9')return false;month=(text[5]-'0')*10+text[6]-'0';if(month<1||month>12)return false;}
    *period=n==4?year:year*100+month;return true;
}
static void begin(sessions_t *s,int64_t now,int64_t epoch,uint32_t date){
    charge_session_t *r=&s->pending_session;memset(r,0,sizeof(*r));s->pending_active=true;
    r->start_epoch=r->end_epoch=date?(uint32_t)(epoch/1000):0;r->start_date=date;
    if(!date)r->flags|=SESSION_CLOCK_GAP;
    s->grid_wh=s->solar_wh=s->unknown_wh=s->cost=s->grid_cost=s->solar_cost=0;s->start_ms=now;s->idle_since=0;
}
void sessions_step(sessions_t *s,int64_t now,int64_t epoch,uint32_t date,
    double watts,int64_t expires,double grid,int64_t grid_expires,float grid_price,float solar_price){
    charge_session_t *r=active(s);
    if(r&&s->initialized&&now>s->last_ms){
        int64_t dt=now-s->last_ms;
        if(dt<=5000){
            int64_t end=now<s->charge_expires?now:s->charge_expires;
            int64_t ms=end-s->last_ms;
            if(ms>0&&bounded(s->previous_w,1000000)&&s->previous_w>0){
                int64_t known_end=end<s->grid_expires?end:s->grid_expires;
                int64_t known=known_end>s->last_ms?known_end-s->last_ms:0;
                if(!isfinite(s->previous_grid))known=0;
                double from_grid=fmin(s->previous_w,fmax(0,s->previous_grid));
                /* If the house meter has a gap, account the unmatched energy as
                   grid energy. This keeps the bill conservative and avoids an
                   unexplained third origin in the UI and CSV export. */
                double grid_wh=(known?from_grid*known:0)+s->previous_w*(ms-known);
                grid_wh/=3600000.0;
                double solar_wh=known?(s->previous_w-from_grid)*known/3600000.0:0;
                if(s->metered){s->pending_grid+=grid_wh;s->pending_solar+=solar_wh;s->pending_grid_cost+=grid_wh*s->grid_price/1000;s->pending_solar_cost+=solar_wh*s->solar_price/1000;}
                else{s->grid_wh+=grid_wh;s->solar_wh+=solar_wh;s->grid_cost+=grid_wh*s->grid_price/1000;s->solar_cost+=solar_wh*s->solar_price/1000;}
                s->cost=s->grid_cost+s->solar_cost;
                r->grid_wh=(float)s->grid_wh;r->solar_wh=(float)s->solar_wh;r->unknown_wh=(float)s->unknown_wh;r->cost_eur=(float)s->cost;
                r->grid_cost_eur=(float)s->grid_cost;r->solar_cost_eur=(float)s->solar_cost;
                if(s->previous_w>=1000&&r->charging_ms<=UINT32_MAX-(uint32_t)ms)r->charging_ms+=(uint32_t)ms;
                if(date&&(!s->metered||s->previous_w>=1000)){r->end_epoch=(uint32_t)(epoch/1000);if(!r->start_epoch){r->start_epoch=(uint32_t)((epoch-(now-s->start_ms))/1000);r->start_date=date;}}
                if(!date||llabs((epoch-s->last_epoch)-dt)>2000)r->flags|=SESSION_CLOCK_GAP;
            }
            if(end<now)r->flags|=SESSION_METER_GAP;
        }else r->flags|=SESSION_METER_GAP;
        promote(s);
    }
    bool measured=expires>now&&bounded(watts,1000000);
    if(measured&&watts>=1000&&!active(s))begin(s,now,epoch,date);
    r=active(s);
    if(r){
        if(measured&&(s->metered?watts>=1000:watts>0))s->idle_since=0;
        else{if(!s->idle_since)s->idle_since=now;if(now-s->idle_since>=SESSION_PAUSE_MS){if(s->store.active_slot)s->checkpoint=true;s->store.active_slot=0;s->pending_active=false;}}
    }
    s->initialized=true;s->last_ms=now;s->last_epoch=epoch;s->previous_w=watts;s->previous_grid=grid;
    s->charge_expires=bounded(watts,1000000)?expires:0;s->grid_expires=isfinite(grid)?grid_expires:0;
    s->grid_price=bounded(grid_price,10)?grid_price:.25f;s->solar_price=bounded(solar_price,10)?solar_price:.08f;
}
void sessions_meter_mode(sessions_t *s,bool enabled,bool new_session){
    if(s->metered!=enabled||new_session){s->pending_grid=s->pending_solar=s->pending_grid_cost=s->pending_solar_cost=0;}
    if(new_session&&active(s)){if(s->store.active_slot)s->checkpoint=true;s->store.active_slot=0;s->pending_active=false;}
    s->metered=enabled;
}
void sessions_add_metered(sessions_t *s,int64_t now,int64_t epoch,uint32_t date,double wh,bool gap){
    if(!s||!s->metered||!bounded(wh,1000000))return;
    if(wh==0){s->pending_grid=s->pending_solar=s->pending_grid_cost=s->pending_solar_cost=0;return;}
    if(!active(s))begin(s,now,epoch,date);
    s->idle_since=0;
    charge_session_t *r=active(s);double pending=s->pending_grid+s->pending_solar;
    if(!gap&&pending>0){double scale=wh/pending;s->grid_wh+=s->pending_grid*scale;s->solar_wh+=s->pending_solar*scale;s->grid_cost+=s->pending_grid_cost*scale;s->solar_cost+=s->pending_solar_cost*scale;}
    else{s->grid_wh+=wh;s->grid_cost+=wh*s->grid_price/1000;r->flags|=SESSION_METER_GAP;}
    s->pending_grid=s->pending_solar=s->pending_grid_cost=s->pending_solar_cost=0;
    s->cost=s->grid_cost+s->solar_cost;r->grid_wh=(float)s->grid_wh;r->solar_wh=(float)s->solar_wh;r->unknown_wh=(float)s->unknown_wh;
    r->grid_cost_eur=(float)s->grid_cost;r->solar_cost_eur=(float)s->solar_cost;r->cost_eur=r->grid_cost_eur+r->solar_cost_eur;
    if(date)r->end_epoch=(uint32_t)(epoch/1000);
    promote(s);
}
