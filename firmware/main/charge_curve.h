#ifndef CHARGE_CURVE_H
#define CHARGE_CURVE_H
#include <stdbool.h>
#include <stdint.h>

/* Bounded session history, independent of the billing/energy counters. Older
 * samples are combined when full; the beginning and measured extrema remain. */
#define CHARGE_CURVE_POINTS 256
typedef struct { uint32_t s; float w,target_w,low_w,high_w; } charge_curve_point_t;
typedef struct {
    uint32_t version,count,interval_s,duration_s,start_epoch,end_epoch;
    uint32_t started_at,last_sample_at,revision;
    bool active,interrupted;
    charge_curve_point_t live,points[CHARGE_CURVE_POINTS];
} charge_curve_t;

void charge_curve_step(charge_curve_t *curve,uint32_t now,uint32_t epoch,
    bool requested,bool finished,bool fresh,bool flowing,float watts,float target_w);
bool charge_curve_restore(charge_curve_t *out,const charge_curve_t *saved);
bool charge_curve_valid(const charge_curve_t *curve);
#endif
