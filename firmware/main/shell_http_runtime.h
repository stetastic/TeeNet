#pragma once
#include "shell_http_control.h"

/* Persistent opt-in, independent of browsers and private test leases. The
   pilot is permission feedback, never a physical current/zero measurement. */
typedef struct { shell_http_control_t control; bool running; } shell_http_runtime_t;

static inline bool shell_http_runtime_ready(const shell_http_runtime_t *r,
    const shell_http_sample_t *sample,int64_t at,int64_t now,bool vehicle_known){
    return r->running&&vehicle_known&&shell_http_trial_ready(&r->control,sample,at,now);
}
/* Run only on the controller clock. False means revoke charging and require
   a new explicit Start; restored HTTP data alone never resumes a failed run. */
static inline bool shell_http_runtime_tick(shell_http_runtime_t *r,bool selected,bool enabled,
    float offset,const shell_http_sample_t *sample,int64_t at,int64_t now,bool vehicle_known){
    if(!selected){*r=(shell_http_runtime_t){0};return true;}
    if(!enabled){r->running=false;r->control.armed=false;return true;}
    if(!r->running){
        shell_http_trial_arm(&r->control,now);
        r->control.lease_until=INT64_MAX;r->control.normal_offset_a=offset;r->running=true;
    }
    if(!shell_http_runtime_ready(r,sample,at,now,vehicle_known)){
        shell_http_trial_fail(&r->control);return false;
    }
    return true;
}
/* A policy estimate for PV allocation and anti-cycle logic. Never copy this
   into actual_a, feed metered energy, or use it for contactor zero-current proof. */
static inline void shell_http_runtime_currents(bool enabled,bool fault,
    const shell_http_sample_t *sample,int64_t at,int64_t now,unsigned phases,float out[3]){
    for(unsigned p=0;p<3;p++)out[p]=0;
    if(!enabled||fault||!shell_http_trial_fresh(sample,at,now)||!sample->car_charging||
       (phases!=1&&phases!=3))return;
    float current=fmaxf(0,fminf(16,sample->pilot_a)-.25f);
    for(unsigned p=0;p<phases;p++)out[p]=current;
}
