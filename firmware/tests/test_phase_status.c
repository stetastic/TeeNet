#include "ems_core.h"
#include <assert.h>
#include <stdio.h>
#include <string.h>
enum {PHASE_READY,PHASE_STOPPING,PHASE_MOVING,PHASE_SETTLING,PHASE_FAULT};
static settings_t settings;
static unsigned phase_state,phase_candidate=1;
static int64_t phase_zero_at,phase_stop_at=1000,phase_move_at=2000,phase_hold_until,phase_candidate_at=3000;
#include "phase_status.inc"
static void check(int64_t now,const char *expected,int64_t end){const char *kind;int64_t deadline;phase_wait_status_locked(now,&kind,&deadline);assert(!strcmp(kind,expected)&&deadline==end);}
int main(void){
    settings_defaults(&settings);phase_state=PHASE_STOPPING;check(2000,"none",0);
    settings.phase_switch_enabled=true;check(2000,"current_zero",61000);
    phase_zero_at=2000;check(3000,"zero_hold",10000);
    check(10000,"fresh_data",61000); /* elapsed hold still needs a new zero measurement */
    expert_values_t e;expert_defaults(&e);e.values[EP_PHASE_ZERO]=15;e.values[EP_PHASE_STOP_MAX]=90;
    e.values[EP_PHASE_FEEDBACK]=8;e.values[EP_PHASE_SETTLE]=12;e.values[EP_PHASE_DATA_MAX]=60;
    e.values[EP_PHASE_DOWN]=45;e.values[EP_PHASE_UP]=180;assert(expert_activate(&e));
    check(3000,"zero_hold",17000);phase_zero_at=0;check(3000,"current_zero",91000);
    phase_state=PHASE_MOVING;settings.phase_feedback_enabled=true;check(3000,"contact",10000);
    settings.phase_feedback_enabled=false;check(3000,"motion",4000);
    phase_state=PHASE_SETTLING;check(13999,"settle",14000);check(14000,"fresh_data",62000);
    phase_state=PHASE_READY;settings.enabled=true;settings.mode=MODE_PV;
    check(4000,"candidate",48000);phase_candidate=3;check(4000,"candidate",183000);
    phase_hold_until=10000;check(4000,"hold",10000);check(10000,"candidate",183000);
    settings.enabled=false;check(4000,"none",0);settings.enabled=true;phase_state=PHASE_FAULT;check(4000,"none",0);
    puts("PASS: phase countdowns match active expert parameters, switching states, hold/candidate deadlines and disabled/fault states");
}
