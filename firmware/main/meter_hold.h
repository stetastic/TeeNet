#ifndef METER_HOLD_H
#define METER_HOLD_H
#include "ems_core.h"
#include <math.h>
#include <string.h>
#define WALLBOX_METER_HOLD_MS 60000
typedef struct {int64_t sample_at;float target_a;unsigned phases;} meter_hold_t;
static inline void meter_hold_sample(meter_hold_t *h,const settings_t *s,bool network,bool ready,
    int64_t now,int64_t sample_at,const float amps[3],float target) {
    if(!s->enabled||!network||!ready||target<=0){memset(h,0,sizeof(*h));return;}
    if(!fresh(now,sample_at,5000)||sample_at==h->sample_at)return;
    bool flow=false;
    for(unsigned i=0;i<3;i++){if(!isfinite(amps[i])||amps[i]<0||amps[i]>999)return;if(amps[i]>=1)flow=true;}
    *h=(meter_hold_t){.sample_at=sample_at,.target_a=flow?target:0,.phases=s->charge_phases};
}
static inline bool meter_hold_active(const meter_hold_t *h,const settings_t *s,bool network,bool ready,int64_t now,int64_t sample_at) {
    return network&&ready&&s->enabled&&s->mode!=MODE_OFF&&h->target_a>=s->min_charge_a&&
        h->phases==s->charge_phases&&h->sample_at==sample_at&&sample_at>0&&now>=sample_at+5000&&
        now-sample_at<WALLBOX_METER_HOLD_MS;
}
static inline void meter_hold_report(const settings_t *s,bool active,float target,float report[3]) {
    if(!active||target<=0)return;
    /* Neutral DLB balance: cached current error must not repeatedly accelerate
       or decelerate the Shell. Explicit stop and house protection still win. */
    for(unsigned i=0;i<3;i++)report[i]=i<s->charge_phases?s->grid_limit_a:0;
}
#endif
