#include "charge_curve.h"
#include <math.h>
#include <string.h>

static bool power_valid(float w) { return isnan(w)||(isfinite(w)&&w>=0&&w<=1000000); }
static bool point_valid(const charge_curve_point_t *p) {
    return power_valid(p->w)&&power_valid(p->target_w)&&power_valid(p->low_w)&&power_valid(p->high_w)&&
        (isnan(p->w)?((isnan(p->low_w)&&isnan(p->high_w))||p->low_w<=p->high_w):p->low_w<=p->w&&p->w<=p->high_w);
}
bool charge_curve_valid(const charge_curve_t *c) {
    if(c->version!=1||!c->count||c->count>CHARGE_CURVE_POINTS||c->interval_s<5||
        c->interval_s>86400||c->duration_s>31536000||c->points[0].s!=0||
        c->live.s!=c->duration_s||!point_valid(&c->live))return false;
    uint32_t previous=0;
    for(uint32_t i=0;i<c->count;i++) {
        const charge_curve_point_t *p=&c->points[i];
        if(p->s>c->duration_s||(i&&p->s<=previous)||!point_valid(p))return false;
        previous=p->s;
    }
    return !c->start_epoch||!c->end_epoch||c->end_epoch>=c->start_epoch;
}
bool charge_curve_restore(charge_curve_t *out,const charge_curve_t *saved) {
    if(!charge_curve_valid(saved))return false;
    *out=*saved;
    if(out->active){out->active=false;out->interrupted=true;out->end_epoch=out->start_epoch?out->start_epoch+out->duration_s:0;}
    return true;
}
static void append(charge_curve_t *c,charge_curve_point_t p) {
    if(c->count&&c->points[c->count-1].s==p.s){c->points[c->count-1]=p;return;}
    if(c->count==CHARGE_CURVE_POINTS) {
        uint32_t n=1;
        for(uint32_t i=1;i+1<c->count;i+=2) {
            charge_curve_point_t a=c->points[i],b=c->points[i+1],m=b;
            /* A missing measurement remains a gap instead of becoming zero. */
            m.w=isfinite(a.w)&&isfinite(b.w)?(a.w+b.w)/2:NAN;
            m.low_w=fminf(a.low_w,b.low_w);
            m.high_w=fmaxf(a.high_w,b.high_w);
            m.target_w=isfinite(a.target_w)&&isfinite(b.target_w)?(a.target_w+b.target_w)/2:NAN;
            c->points[n++]=m;
        }
        c->points[n++]=c->points[CHARGE_CURVE_POINTS-1];
        c->count=n;c->interval_s=c->interval_s<43200?c->interval_s*2:86400;
    }
    c->points[c->count++]=p;
}
void charge_curve_step(charge_curve_t *c,uint32_t now,uint32_t epoch,
    bool requested,bool finished,bool fresh,bool flowing,float watts,float target_w) {
    if(!c->active) {
        if(!fresh||!flowing||!isfinite(watts)||watts<0)return;
        uint32_t revision=c->revision+1;
        memset(c,0,sizeof(*c));c->version=1;c->interval_s=5;c->started_at=now;
        c->start_epoch=epoch;c->active=true;c->revision=revision;
    }
    if(now<c->started_at)return;
    c->duration_s=now-c->started_at;
    if(c->duration_s>31536000){c->active=false;c->interrupted=true;return;}
    if(!c->start_epoch&&epoch>=c->duration_s)c->start_epoch=epoch-c->duration_s;
    float w=fresh&&isfinite(watts)&&watts>=0&&watts<=1000000?watts:NAN;
    float goal=isfinite(target_w)&&target_w>=0&&target_w<=1000000?target_w:NAN;
    charge_curve_point_t p={.s=c->duration_s,.w=w,.low_w=w,.high_w=w,.target_w=goal};
    if(c->count&&c->started_at+c->live.s>c->last_sample_at){p.low_w=fminf(c->live.low_w,w);p.high_w=fmaxf(c->live.high_w,w);}
    c->live=p;
    bool ended=fresh&&!flowing&&(!requested||finished);
    bool gap=c->count&&(isfinite(c->points[c->count-1].w)!=isfinite(w));
    if(!c->count||ended||gap||now-c->last_sample_at>=c->interval_s) {
        append(c,p);c->last_sample_at=now;
    }
    if(ended){c->active=false;c->end_epoch=epoch?epoch:c->start_epoch?c->start_epoch+c->duration_s:0;}
    c->revision++;
}
