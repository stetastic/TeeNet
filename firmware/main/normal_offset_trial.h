#pragma once
#include <stdbool.h>
#include <stdint.h>
#include <math.h>
typedef struct {bool armed,fault;float original;int64_t lease_until;} normal_offset_trial_t;
static inline bool normal_offset_trial_begin(normal_offset_trial_t *t,float original,float value,int64_t now){
    if(t->armed||!isfinite(original)||original<0||original>2||!isfinite(value)||value<0||value>2||fabsf(value-original)>.20001f)return false;
    *t=(normal_offset_trial_t){.armed=true,.original=original,.lease_until=now+15000};return true;
}
static inline bool normal_offset_trial_touch(normal_offset_trial_t *t,int64_t now){
    if(!t->armed||t->fault||now>=t->lease_until)return false;
    t->lease_until=now+15000;return true;
}
static inline bool normal_offset_trial_ready(const normal_offset_trial_t *t,int64_t now){
    return !t->armed||(!t->fault&&now<t->lease_until);
}
