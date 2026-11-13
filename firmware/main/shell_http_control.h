#pragma once
#include <math.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include "cJSON.h"
#include "ems_core.h"

/* Shared pilot-feedback regulator. A pilot limit is an actuator state, never measured current.
   The delayed scalar current is for logging/identification, not phase-zero proof. */
typedef struct {
    bool pilot_valid, current_valid, car_charging;
    float pilot_a, current_a;
} shell_http_sample_t;

static inline void shell_http_sample_parse(const cJSON *connector,shell_http_sample_t *out){
    *out=(shell_http_sample_t){0};
    const cJSON *limit=cJSON_GetObjectItemCaseSensitive(connector,"limit");
    const cJSON *pilot=cJSON_GetObjectItemCaseSensitive(limit,"current");
    if(cJSON_IsNumber(pilot)&&isfinite(pilot->valuedouble)&&pilot->valuedouble>=0&&pilot->valuedouble<=32){
        out->pilot_a=(float)pilot->valuedouble;out->pilot_valid=true;
    }
    const cJSON *transaction=cJSON_GetObjectItemCaseSensitive(connector,"transaction");
    const cJSON *car=cJSON_GetObjectItemCaseSensitive(transaction,"statusCar");
    out->car_charging=cJSON_IsString(car)&&!strcmp(car->valuestring,"Charging");
    const cJSON *rate=cJSON_GetObjectItemCaseSensitive(connector,"chargingRate");
    if(cJSON_IsString(rate)){
        float kw=0,current=0;unsigned phases=0;int consumed=0;
        bool parsed=sscanf(rate->valuestring,"Charging rate: %fkw %u phase [%fA %n",&kw,&phases,&current,&consumed)==3&&consumed>0;
        if(!parsed){
            int label=0;consumed=0;
            /* Shell also spells the one-phase label literally; current remains telemetry only. */
            if(sscanf(rate->valuestring,"Charging rate: %fkw %n",&kw,&label)==1&&label>0&&
               !strncmp(rate->valuestring+label,"Single phase [",14)&&
               sscanf(rate->valuestring+label+14,"%fA %n",&current,&consumed)==1&&consumed>0){
                phases=1;consumed+=label+14;parsed=true;
            }
        }
        if(parsed&&isfinite(kw)&&kw>=0&&kw<=30&&isfinite(current)&&current>=0&&current<=40&&
           (phases==1||phases==3)&&strchr(rate->valuestring+consumed,']')){
            out->current_a=current;out->current_valid=true;
        }
    }
}

typedef enum {HTTP_TRIAL_STOPPED,HTTP_TRIAL_START,HTTP_TRIAL_WAIT,HTTP_TRIAL_BALANCE,HTTP_TRIAL_FAULT} shell_http_stage_t;
typedef struct {
    bool armed,fault;
    shell_http_stage_t stage;
    int64_t lease_until,next_step_at,waiting_since,pulse_until;
    float previous_target,waiting_pilot,last_report,step_a,pilot_bias_a,pulse_report;
    unsigned previous_phases,steps,start_pulses;
    bool awaiting;
    int64_t interval_ms,pulse_ms,next_start_at;
    float integral_a; /* Compatibility metadata; no integral correction. */
    /* Captured normal calibration supplies the bounded virtual-current neutral
       compensation. It never shifts the requested pilot goal. */
    float normal_offset_a;
    bool startup_filter_starting,startup_filter_recovery;
    float startup_filter_target;
    int64_t startup_charging_since,startup_charging_sample;
    bool startup_charging_ready;
    int64_t last_sample_at,last_reply_at;
    bool coast_eligible,coast_anchor_valid,coast_reduction_sent,coast_active,coast_waiting_sent;
    bool coast_bulk_used,coast_near_used;
    float coast_anchor_pilot;
    int64_t coast_sample_at,coast_trigger_at,coast_until;
} shell_http_control_t;

#define SHELL_HTTP_TRIAL_TTL_MS 4000
#define SHELL_HTTP_TRIAL_LEASE_MS 15000
#define SHELL_HTTP_TRIAL_STEP_MS 12000
#define SHELL_HTTP_TRIAL_ACK_MS 40000
#define SHELL_HTTP_TRIAL_COAST_MS 8000
#define SHELL_HTTP_TRIAL_ONE_PHASE_START_MS 6000

/* Releasing a hold must not refill its per-target budget, including after an
   undershoot or a temporary return to START. Only response_reset refills it. */
static inline void shell_http_trial_coast_release(shell_http_control_t *c){
    c->coast_eligible=c->coast_anchor_valid=c->coast_reduction_sent=c->coast_active=c->coast_waiting_sent=false;
    c->coast_anchor_pilot=0;c->coast_sample_at=c->coast_trigger_at=c->coast_until=0;
}
static inline void shell_http_trial_response_reset(shell_http_control_t *c){
    c->startup_filter_starting=c->startup_filter_recovery=false;
    c->startup_filter_target=0;
    c->startup_charging_since=c->startup_charging_sample=0;c->startup_charging_ready=false;
    c->coast_bulk_used=c->coast_near_used=false;shell_http_trial_coast_release(c);
}
static inline void shell_http_trial_fail(shell_http_control_t *c){
    c->fault=true;c->stage=HTTP_TRIAL_FAULT;shell_http_trial_response_reset(c);
}

static inline bool shell_http_trial_fresh(const shell_http_sample_t *s,int64_t at,int64_t now){
    return s&&s->pilot_valid&&isfinite(s->pilot_a)&&s->pilot_a>=0&&s->pilot_a<=32&&
        at>0&&now>=at&&now-at<SHELL_HTTP_TRIAL_TTL_MS;
}
static inline void shell_http_trial_arm(shell_http_control_t *c,int64_t now){
    *c=(shell_http_control_t){.armed=true,.lease_until=now+SHELL_HTTP_TRIAL_LEASE_MS,
        .normal_offset_a=.8f,.pilot_bias_a=.25f,.step_a=1.0f,.interval_ms=SHELL_HTTP_TRIAL_STEP_MS,.pulse_ms=6000};
}
static inline bool shell_http_trial_ready(const shell_http_control_t *c,const shell_http_sample_t *s,int64_t at,int64_t now){
    return c->armed&&!c->fault&&now<c->lease_until&&shell_http_trial_fresh(s,at,now);
}
static inline const char *shell_http_trial_stage(shell_http_stage_t stage){
    static const char *names[]={"stopped","starting","observing","balanced","fault"};
    return (unsigned)stage<5?names[stage]:"fault";
}
/* HTTP feedback controller. One bounded pilot goal drives both the
   correction and the state indication. Normal calibration supplies only the
   output neutral (grid + offset), including inside the pilot deadband. This
   application applies bounded output smoothing. Step/interval/pulse fields remain
   compatibility metadata and do not tune this feedback.
   The commissioned7c START handover requires statusCar=Charging. An8-A pilot
   while the car is still waiting does not revoke startup headroom: doing so
   failed the184733 hardware trial before sustained current was established.
   No delayed measured current participates. Status GET operates on a copy. */
static inline void shell_http_trial_report(shell_http_control_t *c,const shell_http_sample_t *s,
    int64_t sample_at,int64_t now,float target,unsigned phases,float grid,float minimum,float maximum,
    bool advance,float out[3]){
    shell_http_control_t copy=*c;if(!advance)c=&copy;
    float stop=grid+maximum+1;
    for(unsigned p=0;p<3;p++)out[p]=p<phases?stop:0;
    if(!isfinite(target)||!isfinite(grid)||!isfinite(minimum)||!isfinite(maximum)||
       (phases!=1&&phases!=3)||minimum<6||maximum>16||maximum<minimum||
       !isfinite(c->pilot_bias_a)||c->pilot_bias_a<-.5f||c->pilot_bias_a>.8f||
       !isfinite(c->normal_offset_a)||c->normal_offset_a<0||c->normal_offset_a>2){shell_http_trial_fail(c);return;}
    if(!shell_http_trial_ready(c,s,sample_at,now)){
        shell_http_trial_fail(c);return;
    }
    if(target<=0){shell_http_trial_response_reset(c);c->stage=HTTP_TRIAL_STOPPED;c->awaiting=false;c->pulse_until=0;c->next_start_at=0;c->start_pulses=0;c->previous_target=0;c->previous_phases=phases;c->last_report=stop;c->integral_a=0;c->last_sample_at=c->last_reply_at=0;return;}
    if(target<minimum||target>maximum){shell_http_trial_fail(c);return;}
    float goal=fminf(maximum,fmaxf(minimum,target+c->pilot_bias_a));
    bool changed=c->previous_phases!=phases||fabsf(target-c->previous_target)>.01f;
    if(changed){c->previous_target=target;c->previous_phases=phases;c->waiting_since=now;
        shell_http_trial_response_reset(c);}
    /* A previously initialized private3p START keeps its original deadline,
       even if the first qualifying Charging receipt arrives after40s. Target
       or phase changes have already cleared the startup flag above. */
    bool three_start=phases==3&&c->startup_filter_starting&&c->stage==HTTP_TRIAL_START;
    if(three_start&&now-c->waiting_since>SHELL_HTTP_TRIAL_ACK_MS){shell_http_trial_fail(c);return;}
    /* Charging can precede established flow. One-phase retains its existing
       six-second guard. Three-phase tracks receipts only during initialized
       START and may hand over sooner when the fresh pilot reaches the
       commissioned startup threshold. A lower running floor must not revoke
       startup headroom on the initial 8-A permission alone.
       This remains an empirical guard, never proof of physical current. */
    if((phases==1||three_start)&&s->car_charging&&s->pilot_a>=.1f){
        if(sample_at!=c->startup_charging_sample){
            if(!c->startup_charging_sample||sample_at<c->startup_charging_sample||
               sample_at-c->startup_charging_sample>=SHELL_HTTP_TRIAL_TTL_MS){
                c->startup_charging_since=sample_at;c->startup_charging_ready=false;
            }
            c->startup_charging_sample=sample_at;
            if(sample_at>c->startup_charging_since+SHELL_HTTP_TRIAL_ONE_PHASE_START_MS)
                c->startup_charging_ready=true;
        }
    }else{
        c->startup_charging_since=c->startup_charging_sample=0;c->startup_charging_ready=false;
    }
    float report;
    float error=goal-s->pilot_a,neutral=grid+c->normal_offset_a;
    float startup_minimum=fmaxf(minimum,8.7f);
    bool three_pending=three_start&&s->car_charging&&s->pilot_a>=.1f&&
        s->pilot_a<startup_minimum&&!c->startup_charging_ready;
    if(s->pilot_a<.1f||!s->car_charging||three_pending){
        if(c->stage!=HTTP_TRIAL_START){c->stage=HTTP_TRIAL_START;c->waiting_since=now;}
        if(now-c->waiting_since>SHELL_HTTP_TRIAL_ACK_MS){shell_http_trial_fail(c);return;}
        shell_http_trial_coast_release(c);
        report=fmaxf(0,grid-fminf(goal,8.0f));
    }else{
        report=fmaxf(0,neutral-error);
        /* Private3p long-hold trial192533 drifted beyond the goal band before
           the former .25-A deadband reacted. Start the existing small ramp
           earlier; one-phase and the independent coast thresholds stay .25. */
        float feedback_deadband=phases==3?.15f:.25f;
        if(fabsf(error)<feedback_deadband)report=neutral;
        c->stage=fabsf(s->pilot_a-goal)<=.45f?HTTP_TRIAL_BALANCE:HTTP_TRIAL_WAIT;
        if(c->stage==HTTP_TRIAL_BALANCE)c->waiting_since=now;
        else if(now-c->waiting_since>180000){shell_http_trial_fail(c);return;}
        bool low_three=phases==3&&target<=minimum+1.0f;
        if(!low_three||s->pilot_a<goal-.25f){
            /* Do not delay an upward correction after a low-target undershoot. */
            shell_http_trial_coast_release(c);
        }else{
            c->coast_eligible=true;
            if(sample_at>c->coast_sample_at){
                bool near_goal=s->pilot_a<=goal+2.5f;
                bool budget_available=near_goal?!c->coast_near_used:!c->coast_bulk_used;
                if(c->coast_anchor_valid&&c->coast_reduction_sent&&s->pilot_a>goal+.25f&&
                   c->coast_anchor_pilot-s->pilot_a>=.2f&&budget_available){
                    /* At most one bulk and one near-goal hold. Entering the
                       near zone can use its reserved hold during a bulk hold;
                       later declines cannot keep extending either deadline. */
                    if(near_goal)c->coast_near_used=true;else c->coast_bulk_used=true;
                    c->coast_active=c->coast_waiting_sent=true;
                    c->coast_trigger_at=sample_at;c->coast_until=0;c->coast_anchor_pilot=s->pilot_a;
                }else if(!c->coast_anchor_valid||s->pilot_a>c->coast_anchor_pilot||s->pilot_a<=goal+.25f){
                    c->coast_anchor_pilot=s->pilot_a;
                }
                c->coast_anchor_valid=true;c->coast_sample_at=sample_at;
            }
            /* Fixed time alone can resume using the last high pilot just before
               a pending decrease arrives. Require a new reception after the
               deadline; repeated cached copies never release or renew it. */
            if(c->coast_active&&!c->coast_waiting_sent&&c->coast_until>0&&
               now>=c->coast_until&&sample_at>c->coast_until)c->coast_active=false;
            if(c->coast_active)report=neutral;
        }
        /* The 2026-11-10 12 -> 7.8 A jump exhausted the bounded coast, then
           queued another large reduction before the lower permission arrived.
           A first .5-A/.35 taper then took more than 120 s even for 9 -> 7.8 A:
           its output always selected the existing .02-A/s near-goal ramp.
           Bound the bulk correction at 1.2 A so a large error can still use
           the commissioned .3-A/s approach, and taper it toward the goal.
           No measured current enters this path.
           Commissioned targets >= 8.7 A retain their existing gains. */
        if(phases==3&&target<8.7f&&report>neutral)
            report=neutral+fminf(1.2f,.7f*(report-neutral));
        if(sample_at>c->last_sample_at){c->last_sample_at=sample_at;c->steps++;}
    }
    c->last_report=report;c->last_reply_at=now;
    for(unsigned p=0;p<phases;p++)out[p]=report;
}

/* Private handover uses the phase-specific receipt guard above. While3p waits
   at the initial low pilot, explicitly keep the START report; applying the
   ordinary smoother to a tracking report could clamp the filter to grid32
   and revoke startup headroom before the guard. One-phase remains unchanged.
   Non-current reads operate on copies of both states. */
static inline void shell_http_trial_smooth(shell_http_control_t *c,manual_report_filter_t *filter,
    const settings_t *s,float target,int64_t now,bool advance,float report[3]){
    shell_http_control_t copy=*c;manual_report_filter_t filter_copy=*filter;
    if(!advance){c=&copy;filter=&filter_copy;}
    unsigned phases=s->charge_phases;
    bool private_manual=c->armed&&(phases==1||phases==3)&&(s->mode==MODE_MANUAL||
        (s->shell_http_control_enabled&&(s->mode==MODE_PV||s->mode==MODE_GRID_LIMIT)));
    bool valid=private_manual&&!c->fault&&isfinite(target)&&target>0&&
        isfinite(c->normal_offset_a)&&c->normal_offset_a>=0&&c->normal_offset_a<=2;
    for(unsigned p=0;p<phases&&valid;p++)
        valid=isfinite(report[p])&&report[p]<s->grid_limit_a+s->max_charge_a+1;
    if(!valid||c->stage==HTTP_TRIAL_STOPPED||c->stage==HTTP_TRIAL_FAULT){
        c->startup_filter_starting=c->startup_filter_recovery=false;
        manual_report_smooth_mode(filter,s,target,now,report,private_manual);return;
    }
    if(c->stage==HTTP_TRIAL_START){
        c->startup_filter_starting=true;c->startup_filter_recovery=false;c->startup_filter_target=target;
        if(phases==3&&c->startup_charging_sample>0){
            for(unsigned p=0;p<3;p++)filter->reported[p]=report[p];
            filter->active=true;filter->last_ms=now;return;
        }
        manual_report_smooth_mode(filter,s,target,now,report,private_manual);return;
    }
    float neutral=s->grid_limit_a+c->normal_offset_a;
    bool same_target=fabsf(target-c->startup_filter_target)<=.01f;
    if(!same_target)c->startup_filter_starting=c->startup_filter_recovery=false;
    if(phases==1&&c->startup_filter_starting&&!c->startup_charging_ready){
        manual_report_smooth_mode(filter,s,target,now,report,private_manual);return;
    }
    if(c->startup_filter_starting){
        c->startup_filter_starting=false;c->startup_filter_recovery=true;
        for(unsigned p=0;p<3;p++)filter->reported[p]=report[p]=p<phases?fminf(report[p],neutral):0;
        filter->active=true;filter->last_ms=now;
    }else{
        bool lower_descent=phases==3&&target<8.7f;
        if((c->startup_filter_recovery||lower_descent)&&filter->active)
            for(unsigned p=0;p<phases;p++)if(report[p]>=neutral&&filter->reported[p]<neutral)filter->reported[p]=neutral;
        /* The 7.8-A smoke needed 103 s to reach the pilot band. A downstep
           can inherit an obsolete increasing output below the neutral, and
           the normal low-floor smoother then spends .02 A/s crossing the
           whole intermediate range. Revoke that obsolete increase above.
           For a bounded medium correction use .08 A/s, at most .16 A per
           frame; retain the existing .02/.04 near-goal and bulk behaviour.
           Coasting, Stop, START and upward recovery never enter this boost. */
        float requested[3]={report[0],report[1],report[2]};
        manual_report_filter_t before=*filter;
        manual_report_smooth_mode(filter,s,target,now,report,private_manual);
        if(lower_descent&&before.active&&now>before.last_ms&&now-before.last_ms<=5000){
            float step=fminf(.16f,.08f*(now-before.last_ms)/1000.0f);
            for(unsigned p=0;p<3;p++){
                float correction=requested[p]-neutral;
                if(correction>.401f&&correction<=1.001f&&requested[p]>before.reported[p])
                    filter->reported[p]=report[p]=fmaxf(report[p],fminf(requested[p],before.reported[p]+step));
            }
        }
    }
    if(c->startup_filter_recovery&&report[0]>=neutral&&(phases==1||(report[1]>=neutral&&report[2]>=neutral)))
        c->startup_filter_recovery=false;
}

/* Called only after a successful current-register UART write, with the final
   frame values after all safety gates. The eight-second coast starts here,
   not at a GET, proposed output, failed write, or a STOP substituted downstream.
   The prepared-reply identity also rejects callbacks from an obsolete arm. */
static inline void shell_http_trial_transmitted(shell_http_control_t *c,int64_t prepared_at,
    int64_t trigger_at,int64_t sent_at,float target,unsigned phases,float neutral,float stop,const float report[3]){
    if(!c->armed||c->fault||!c->coast_eligible||prepared_at<=0||c->last_reply_at!=prepared_at||
       sent_at<prepared_at||phases!=3||c->previous_phases!=3||!isfinite(target)||target<=0||fabsf(c->previous_target-target)>.01f||
       !isfinite(neutral)||!isfinite(stop)||stop<=neutral)return;
    bool reducing=true,at_neutral=true;
    for(unsigned p=0;p<3;p++){
        if(!isfinite(report[p])||report[p]<0||report[p]>=stop)return;
        reducing=reducing&&report[p]>neutral+.001f;
        at_neutral=at_neutral&&fabsf(report[p]-neutral)<.001f;
    }
    if(reducing)c->coast_reduction_sent=true;
    if(at_neutral&&c->coast_active&&c->coast_waiting_sent&&trigger_at>0&&trigger_at==c->coast_trigger_at){
        c->coast_waiting_sent=false;c->coast_until=sent_at+SHELL_HTTP_TRIAL_COAST_MS;
    }
}
