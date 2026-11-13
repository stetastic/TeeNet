#ifndef SOC_GOAL_H
#define SOC_GOAL_H
#include "ems_features.h"
#include <math.h>
#include <string.h>
typedef struct {
    uint32_t version;
    bool active,manual;
    float capacity_kwh,efficiency,target_soc,last_soc,anchor_wh,manual_current_a;
    uint8_t manual_phases;
} soc_goal_t;
static inline bool soc_value_valid(float value){return isfinite(value)&&value>=0&&value<=100;}
static inline bool soc_goal_valid(const soc_goal_t *g,const charge_plan_t *p){
    if(!g->version)return !g->active&&!g->manual;
    if(g->version!=1)return false;
    if(g->manual&&g->manual_current_a!=0&&(!isfinite(g->manual_current_a)||g->manual_current_a<6||g->manual_current_a>32||(g->manual_phases!=1&&g->manual_phases!=3)))return false;
    return !g->active||(isfinite(g->capacity_kwh)&&g->capacity_kwh>=5&&g->capacity_kwh<=200&&
        isfinite(g->efficiency)&&g->efficiency>=.5f&&g->efficiency<=1&&soc_value_valid(g->target_soc)&&g->target_soc>0&&
        soc_value_valid(g->last_soc)&&isfinite(g->anchor_wh)&&g->anchor_wh>=0&&g->anchor_wh<=p->delivered_wh);
}
static inline void soc_goal_clear(soc_goal_t *g,bool manual){*g=(soc_goal_t){.version=1,.manual=manual};}
static inline bool soc_goal_begin(soc_goal_t *g,charge_plan_t *p,float capacity,float current,float target,
    int64_t deadline,int64_t now,double total_wh,bool manual){
    if(!isfinite(capacity)||capacity<5||capacity>200||!soc_value_valid(current)||!soc_value_valid(target)||target<=current||
        now<1704067200||deadline<=now||deadline-now>7*86400||!isfinite(total_wh)||total_wh<0)return false;
    float wh=capacity*(target-current)*10/.9f;
    *g=(soc_goal_t){.version=1,.active=true,.manual=manual,.capacity_kwh=capacity,.efficiency=.9f,.target_soc=target,.last_soc=current};
    *p=(charge_plan_t){.version=1,.active=true,.grid=manual,.target_wh=wh,.last_total_wh=total_wh,.deadline_s=deadline};
    return true;
}
static inline float soc_goal_estimate(const soc_goal_t *g,const charge_plan_t *p,float measured){
    if(!g->active)return NAN;
    if(soc_value_valid(measured))return measured;
    return fminf(100,g->last_soc+(p->delivered_wh-g->anchor_wh)*g->efficiency/(g->capacity_kwh*10));
}
static inline bool soc_goal_step(soc_goal_t *g,charge_plan_t *p,bool enabled,float measured){
    if(!g->active||!p->active||!enabled||!soc_value_valid(measured))return false;
    if(measured>=g->target_soc){p->delivered_wh=p->target_wh;return true;}
    if(fabsf(measured-g->last_soc)<.1f)return false;
    p->target_wh=p->delivered_wh+g->capacity_kwh*(g->target_soc-measured)*10/g->efficiency;
    g->last_soc=measured;g->anchor_wh=p->delivered_wh;return true;
}
#endif
