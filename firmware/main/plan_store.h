#ifndef PLAN_STORE_H
#define PLAN_STORE_H
#include "soc_goal.h"
#include "vehicle_profiles.h"
typedef struct {uint32_t version;charge_plan_t plan;vehicle_plan_t vehicle;} vehicle_saved_plan_v2_t;
typedef struct {uint32_t version;charge_plan_t plan;vehicle_plan_t vehicle;soc_goal_t soc;} vehicle_saved_plan_t;
static inline bool plan_store_decode(const void *blob,size_t size,vehicle_saved_plan_t *out){
    if(!blob||!out)return false;
    vehicle_saved_plan_t next={0};
    if(size==sizeof(vehicle_saved_plan_v2_t)){
        vehicle_saved_plan_v2_t old;memcpy(&old,blob,size);if(old.version!=2)return false;
        next.version=3;next.plan=old.plan;next.vehicle=old.vehicle;
    }else if(size==sizeof(next)){memcpy(&next,blob,size);if(next.version!=3)return false;}
    else return false;
    if(!soc_goal_valid(&next.soc,&next.plan))return false;
    if(next.soc.active&&next.vehicle.active)return false;
    *out=next;return true;
}
#endif
