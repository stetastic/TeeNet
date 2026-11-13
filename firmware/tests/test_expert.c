#include "ems_core.h"
#include <assert.h>
#include <math.h>
#include <stdio.h>
#include <string.h>
int main(void){
 expert_values_t defaults,next,decoded;expert_defaults(&defaults);assert(expert_valid(&defaults));
 assert(!expert_modified(NULL));assert(!expert_modified(&defaults));
 next=defaults;next.values[EP_PV_START]=31;assert(expert_modified(&next));
 assert(expert_decode(&next,sizeof(next),&decoded));assert(expert_modified(&decoded));
 next=defaults;next.values[EP_PV_DEFICIT]=0.6f;assert(expert_modified(&next));
 expert_defaults(&next);assert(!expert_modified(&next));
 assert(expert_pin_valid("4040"));assert(!expert_pin_valid("404"));assert(!expert_pin_valid(NULL));
 assert(!expert_pin_valid("4040x"));assert(!expert_pin_valid("0000"));
 next=defaults;next.values[EP_PV_START]=45;assert(expert_activate(&next));
 pv_control_t c={0};for(int64_t t=0;t<45000;t+=1000)pv_control_step(&c,true,12,8.7f,16,t);
 assert(c.target_a==0 && c.wait_ms==1000);pv_control_step(&c,true,12,8.7f,16,45000);assert(c.target_a>9);
 assert(expert_decode(&next,sizeof(next),&decoded));assert(decoded.values[EP_PV_START]==45);
 assert(!expert_decode(&next,sizeof(next)-1,&decoded));
 next.values[EP_PHASE_ZERO]=7;assert(!expert_valid(&next));assert(!expert_activate(&next));
 assert(ems_param(EP_PV_START)==45); /* invalid changes never replace working values */
 next=defaults;next.values[EP_PV_START]=NAN;assert(!expert_valid(&next));
 next=defaults;next.values[EP_PV_START]=30.5f;assert(!expert_valid(&next));
 next=defaults;next.values[EP_PHASE_SETTLE]=30;assert(!expert_valid(&next));
 next=defaults;next.values[EP_EVCC_LEASE]=30;next.values[EP_EVCC_STATUS_TTL]=60;assert(!expert_valid(&next));
 next=defaults;next.values[EP_MIN_CURRENT]=7.5f;assert(expert_valid(&next));next.values[EP_MIN_CURRENT]=5.9f;assert(!expert_valid(&next));
 next=defaults;next.values[EP_STOP_COUNT]=10;assert(expert_valid(&next));
 assert(sizeof(((charge_guard_t *)0)->stops)/sizeof(int64_t)==10);
 next=defaults;next.values[EP_GRID_FALLBACK]=5000;assert(expert_activate(&next));
 settings_t s;settings_defaults(&s);s.grid_guard_enabled=true;s.enabled=true;s.mode=MODE_MANUAL;
 assert(grid_guard_target(&s,16,false)<8); /* runtime parameter reaches real control */
 next=defaults;next.values[EP_PV_UP]=2;next.values[EP_PV_DOWN]=3;next.values[EP_PV_STEP]=1.5f;next.values[EP_PV_LEAD]=0.7f;assert(expert_activate(&next));
 memset(&c,0,sizeof(c));c.initialized=true;c.target_a=10;c.adjusted_at=1000;c.last_at=1000;c.below_at=-1;
 pv_control_step(&c,true,16,8.7f,16,2999);assert(c.target_a==10);
 pv_control_step(&c,true,16,8.7f,16,3000);assert(fabsf(c.target_a-11.5f)<.001f);
 pv_control_step(&c,true,9,8.7f,16,5999);assert(fabsf(c.target_a-11.5f)<.001f);
 pv_control_step(&c,true,9,8.7f,16,6000);assert(c.target_a==9);
 const float measured[3]={10,10,10};assert(fabsf(pv_ramp_available_phases(16,10,measured,3)-10.7f)<.001f);
 assert(expert_activate(&defaults));assert(EMS_PV_START_MS==30000 && EMS_NETWORK_METER_TTL==15000);
 for(unsigned p=0;p<EP_COUNT;p++){assert(expert_meta[p].id[0]);for(unsigned q=p+1;q<EP_COUNT;q++)assert(strcmp(expert_meta[p].id,expert_meta[q].id));}
 puts("PASS: expert defaults, PIN, persisted decode, real PV timing/fallback, bounds, atomic activation and unchanged physical limits");
}
