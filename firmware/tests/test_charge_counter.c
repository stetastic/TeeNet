#include "charge_counter.h"
#include "ems_core.h"
#include "charge_sessions.h"
#include <assert.h>
#include <math.h>
#include <stdio.h>
#include <string.h>
static const int64_t epoch=1793260800000LL;
static charge_counter_input_t input(int64_t ms,double wh){return (charge_counter_input_t){.fresh=true,.present=true,.host="wallbox",.identity="T1",.card="A",.energy_valid=true,.raw_wh=wh,.sample_ms=ms,.now_ms=ms,.epoch_ms=epoch+ms};}
static charge_counter_delta_t step(charge_counter_t *c,int64_t ms,double wh){charge_counter_input_t i=input(ms,wh);return charge_counter_step(c,true,&i);}
static double day(const energy_t *e,unsigned date){for(unsigned j=0;j<EMS_DAYS;j++)if(e->store.days[j].date==date)return e->store.days[j].wh[0];return 0;}
int main(void){
 charge_counter_t c={0};charge_counter_input_t i=input(1000,0);i.idle=true;i.present=false;charge_counter_step(&c,true,&i);
 charge_counter_delta_t d=step(&c,6000,10);assert(d.wh==10&&d.new_session&&c.ready);
 assert(step(&c,6000,10).wh==0&&c.ready);assert(step(&c,11000,25).wh==15);
 i=input(16000,25);i.fresh=false;assert(charge_counter_step(&c,true,&i).wh==0&&!c.ready);
 d=step(&c,31000,60);assert(d.wh==35&&d.gap);assert(step(&c,36000,5).wh==0&&c.resets==1);assert(step(&c,41000,15).wh==10);
 charge_counter_t restored={.saved=c.saved,.restored=true};i=input(1000,40);i.epoch_ms=epoch+51000;d=charge_counter_step(&restored,true,&i);assert(d.wh==25&&d.gap);
 restored=(charge_counter_t){.saved=c.saved,.restored=true};i.epoch_ms=0;assert(charge_counter_step(&restored,true,&i).wh==0&&restored.restored);i.sample_ms=i.now_ms=2000;i.epoch_ms=epoch+52000;assert(charge_counter_step(&restored,true,&i).wh==25);
 i=input(46000,10000);assert(charge_counter_step(&c,true,&i).wh==0&&c.rejected==1);assert(step(&c,51000,10010).wh==10);
 i=input(56000,3);i.identity="T2";d=charge_counter_step(&c,true,&i);assert(d.wh==3&&d.new_session);
 i=input(61000,4);i.identity="";assert(charge_counter_step(&c,true,&i).wh==0&&!c.ready);
 charge_counter_t first={0};assert(step(&first,1000,20000).wh==0);assert(step(&first,6000,20010).wh==10); /* No import of an already counted pre-boot session. */
 energy_t e={.primary_metered=true};double powers[2]={11000,11000};int64_t expiry[2]={20000,20000};
 energy_step(&e,1000,epoch+1000,20261029,epoch,powers,expiry);energy_step(&e,2000,epoch+2000,20261029,epoch,powers,expiry);
 assert(e.store.total_wh[0]==0&&e.store.total_wh[1]>3);energy_add_metered(&e,1000,6000,epoch-2000,epoch+3000,20261029,epoch,10);
 assert(e.store.total_wh[0]==10&&fabs(day(&e,20261028)-4)<.001&&fabs(day(&e,20261029)-6)<.001);
 sessions_t s;sessions_init(&s);sessions_meter_mode(&s,true,false);
 for(int64_t ms=1000;ms<=6000;ms+=1000)sessions_step(&s,ms,epoch+ms,20261029,7200,ms+10000,3600,ms+10000,.25f,.08f);
 sessions_add_metered(&s,6000,epoch+6000,20261029,20,false);const charge_session_t *r=&s.pending_session;assert(s.pending_active&&!s.store.count&&fabs(r->grid_wh-10)<.001&&fabs(r->solar_wh-10)<.001);
 sessions_add_metered(&s,11000,epoch+11000,20261029,30,true);assert(fabs(r->grid_wh+r->solar_wh-50)<.001&&(r->flags&SESSION_METER_GAP));
 sessions_add_metered(&s,12000,epoch+12000,20261029,949,false);assert(!s.store.count);sessions_add_metered(&s,13000,epoch+13000,20261029,1,false);assert(s.store.count==1&&session_recorded(sessions_recent(&s.store,0)));
 sessions_meter_mode(&s,true,true);sessions_add_metered(&s,16000,epoch+16000,20261029,5,false);assert(s.store.count==1&&s.pending_active&&fabs(s.pending_session.grid_wh-5)<.001);
 sessions_init(&s);sessions_meter_mode(&s,true,false);sessions_step(&s,1000,epoch+1000,20261029,7200,20000,0,20000,.25f,.08f);sessions_add_metered(&s,1000,epoch+1000,20261029,1,false);
 sessions_step(&s,2000,epoch+2000,20261029,110,20000,0,20000,.25f,.08f);
 for(int64_t ms=3000;ms<=303000;ms+=1000)sessions_step(&s,ms,epoch+ms,20261029,110,ms+10000,0,ms+10000,.25f,.08f);
 assert(!s.store.active_slot&&!s.pending_active&&!s.store.count&&s.pending_session.end_epoch==(uint32_t)((epoch+2000)/1000)); /* Meter noise cannot extend a stopped charge all night. */
 charge_counter_t soak={0};step(&soak,1000,0);double sum=0;for(unsigned j=1;j<=100000;j++){d=step(&soak,1000+(int64_t)j*5000,j);sum+=d.wh;assert(d.wh==1&&isfinite(sum));}assert(sum==100000);
 puts("PASS: counter deltas, duplicate polls, gaps, resets, session changes, checkpoint restart, jump rejection, midnight split, exact session kWh and 100000-sample soak");
}
