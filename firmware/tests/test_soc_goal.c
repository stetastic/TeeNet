#include "soc_goal.h"
#include "plan_store.h"
#include <assert.h>
#include <stdio.h>
int main(void){
 soc_goal_t g={0};charge_plan_t p={0};int64_t now=1793800000;
 assert(soc_goal_begin(&g,&p,60,97,100,now+3600,now,10000,true));
 assert(g.active&&g.manual&&p.grid&&fabsf(p.target_wh-2000)<.01f);
 p.delivered_wh=1000;assert(fabsf(soc_goal_estimate(&g,&p,NAN)-98.5f)<.001f);
 assert(soc_goal_step(&g,&p,true,99));assert(fabsf(p.target_wh-1666.6667f)<.02f);
 assert(fabsf(soc_goal_estimate(&g,&p,99)-99)<.001f);
 assert(!soc_goal_step(&g,&p,false,100)); /* Explicit stop cannot resume a plan. */
 assert(soc_goal_step(&g,&p,true,100));assert(p.delivered_wh==p.target_wh);
 assert(charge_plan_step(&p,now+60,true,true,10000,6)==PLAN_COMPLETE);
 assert(!soc_goal_begin(&g,&p,0,97,100,now+3600,now,0,true));
 assert(!soc_goal_begin(&g,&p,60,97,95,now+3600,now,0,true));
 assert(!soc_goal_begin(&g,&p,60,NAN,100,now+3600,now,0,true));
 assert(soc_goal_begin(&g,&p,10,97,98,now+3600,now,1000,false));
 assert(p.target_wh<500); /* Percentage goals need no artificial 0.5-kWh floor. */
 vehicle_saved_plan_t stored={.version=3,.plan=p,.soc=g},decoded={0};
 assert(plan_store_decode(&stored,sizeof(stored),&decoded)&&decoded.soc.active&&decoded.soc.target_soc==98);
 vehicle_saved_plan_v2_t old={.version=2,.plan=p};
 assert(plan_store_decode(&old,sizeof(old),&decoded)&&!decoded.soc.active&&decoded.plan.active);
 stored.soc.capacity_kwh=NAN;assert(!plan_store_decode(&stored,sizeof(stored),&decoded));
 puts("PASS: direct 97%-to-100% goal, metered estimation, measured re-anchoring, target completion, no auto-resume, small goals and v2/v3 storage migration");
}
