#pragma once
#include "shell_http_control.h"

/* Presentation only. Never feed this estimate into actual_a, energy counters,
   overload protection or a phase-zero decision. Pilot is permission, not proof
   of current draw. Independent diagnostics retain the measured values. */
typedef struct {bool valid;float current[3],watts;} http_display_estimate_t;
/* Measured pilot-to-current drop, independent of the command's goal bias. */
#define SHELL_HTTP_DISPLAY_CURRENT_BIAS_A .25f
static inline http_display_estimate_t http_display_estimate(const shell_http_sample_t *sample,
    int64_t sample_at,int64_t now,bool enabled,bool aborted,float target,unsigned phases,
    float bias,const float voltage[3],float power_factor){
    http_display_estimate_t out={0};
    if(!enabled||aborted||(isfinite(target)&&target<=0)){out.valid=true;return out;}
    if(!isfinite(target)||target<0||(phases!=1&&phases!=3)||!isfinite(bias)||
       bias<-.5f||bias>.8f||!shell_http_trial_fresh(sample,sample_at,now)||
       !isfinite(power_factor)||power_factor<=0||power_factor>1)return out;
    if(!sample->car_charging||sample->pilot_a<=0){out.valid=true;return out;}
    float current=fmaxf(0,fminf(16,sample->pilot_a)-bias);
    current=fminf(16,current);
    for(unsigned p=0;p<phases;p++){
        if(!isfinite(voltage[p])||voltage[p]<180||voltage[p]>270)return (http_display_estimate_t){0};
        out.current[p]=current;out.watts+=current*voltage[p]*power_factor;
    }
    out.valid=true;return out;
}
