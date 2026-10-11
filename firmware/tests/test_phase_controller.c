#include "ems_core.h"
#include <assert.h>
#include <math.h>
#include <stdio.h>
#include <string.h>
enum {PHASE_READY,PHASE_STOPPING,PHASE_MOVING,PHASE_SETTLING,PHASE_FAULT};
static settings_t settings;
static bool phase_gpio_ready=true,phase_idle_inhibit,phase_startup,restarting,ota_in_progress;
static unsigned phase_state,observed=3;
static uint8_t phase_goal=3,phase_candidate=3;
static int64_t phase_candidate_at,phase_stop_at,phase_zero_at,phase_move_at,phase_hold_until,phase_attempt_at,phase_no_load_since;
static int64_t actual_at,meter_at,wallbox_at,last_sent_current_at,sample_at,relay_changed_at;
static float actual_a[3],solar_w=3300;
static bool relay_on[1],fresh_meter=true;
static pv_control_t pv_control;
static manual_report_filter_t manual_filter;
static struct {bool configured,enabled,preparing;unsigned phases;} evcc_control;
static int64_t meter_ttl=5000;
static int64_t wallbox_meter_ttl(void){return meter_ttl;}
static unsigned phase_observed(void){return observed;}
static unsigned evcc_desired_phases(const void *unused,unsigned active){(void)unused;return evcc_control.enabled||evcc_control.preparing?evcc_control.phases:active;}
static float phase_surplus_w_locked(int64_t now){(void)now;return solar_w;}
static void relay_set(unsigned index,bool on){
    assert(index==0 && actual_a[0]<1 && actual_a[1]<1 && actual_a[2]<1);
    assert(phase_zero_at>0 && sample_at-phase_zero_at>=ems_param_ms(EP_PHASE_ZERO));
    relay_on[0]=on;observed=on?1:3;relay_changed_at=sample_at;
}
#include "phase_controller.inc"
static void fixture(unsigned phases){
    settings_defaults(&settings);settings.phase_switch_enabled=settings.relay_board_enabled=settings.phase_feedback_enabled=true;
    settings.charge_phases=phases;settings.enabled=settings.zero_feed_enabled=true;settings.mode=MODE_PV;
    phase_state=PHASE_READY;phase_candidate=phase_goal=observed=phases;relay_on[0]=phases==1;
    phase_candidate_at=phase_stop_at=phase_zero_at=phase_move_at=phase_hold_until=phase_attempt_at=phase_no_load_since=relay_changed_at=0;
    phase_idle_inhibit=phase_startup=restarting=ota_in_progress=false;fresh_meter=phase_gpio_ready=true;meter_ttl=5000;
    memset(&evcc_control,0,sizeof(evcc_control));memset(&pv_control,0,sizeof(pv_control));memset(&manual_filter,0,sizeof(manual_filter));solar_w=3300;
}
static void step(int64_t now,float amps){
    sample_at=now;actual_at=fresh_meter?now:0;meter_at=wallbox_at=last_sent_current_at=now;
    actual_a[0]=amps;actual_a[1]=actual_a[2]=0;phase_step_locked(now);
}
int main(void){
    /* Even evcc's ongoing one-phase request cannot bypass restart parking. */
    fixture(1);evcc_control.configured=evcc_control.enabled=true;evcc_control.phases=1;
    restarting=true;settings.enabled=false;step(1000,10);step(2000,10);assert(phase_state==PHASE_STOPPING&&phase_goal==3);
    step(3000,0);for(int64_t t=4000;t<=10000;t+=1000){step(t,0);assert(relay_on[0]);}
    step(11000,0);assert(!relay_on[0]&&relay_changed_at==11000&&phase_state==PHASE_MOVING);
    step(12000,0);assert(settings.charge_phases==3&&phase_state==PHASE_SETTLING);
    for(int64_t t=13000;t<=17000;t+=1000)step(t,0);
    assert(phase_state==PHASE_READY&&phase_hold_until==0);
    /* An OTA request cancels a pending change TO one phase before the relay. */
    fixture(3);phase_state=PHASE_STOPPING;phase_goal=1;phase_stop_at=1000;ota_in_progress=true;
    step(2000,0);assert(phase_goal==3&&!relay_on[0]);step(10000,0);assert(!relay_on[0]);
    /* A new load resets continuous zero confirmation. */
    fixture(1);restarting=true;settings.enabled=false;step(1000,4);step(2000,4);step(3000,0);step(9000,2);
    assert(phase_zero_at==0&&relay_on[0]);step(10000,0);step(17000,0);assert(relay_on[0]);step(18000,0);assert(!relay_on[0]);
    /* Lost measurements never release an energized contactor for restart. */
    fixture(1);restarting=true;settings.enabled=false;step(1000,0);step(2000,0);fresh_meter=false;
    step(3000,0);step(63000,0);assert(phase_state==PHASE_FAULT&&relay_on[0]);
    /* Idle PV starts do not stack a separate phase dwell ahead of PV validation. */
    fixture(3);phase_startup=true;step(1000,0);step(2000,0);assert(phase_state==PHASE_STOPPING&&phase_goal==1);
    for(int64_t t=3000;t<=11000;t+=1000)step(t,0);assert(relay_on[0]);
    step(12000,0);for(int64_t t=13000;t<=17000;t+=1000)step(t,0);
    assert(phase_state==PHASE_READY&&settings.charge_phases==1&&phase_hold_until==0);
    /* Feedback disagreement blocks charging without another relay command. */
    observed=3;step(18000,0);assert(phase_state==PHASE_FAULT&&relay_on[0]);
    /* A network meter's long TTL must not turn ONE cached zero sample into
       confirmation of eight seconds of current-free operation. */
    fixture(1);meter_ttl=15000;restarting=true;settings.enabled=false;
    step(1000,10);step(2000,10);step(3000,0);
    sample_at=wallbox_at=last_sent_current_at=11000;phase_step_locked(11000);
    assert(relay_on[0]&&phase_state==PHASE_STOPPING);
    step(12000,0);assert(!relay_on[0]&&phase_state==PHASE_MOVING);
    /* A measurement from before the stop request cannot begin confirmation. */
    fixture(1);meter_ttl=15000;restarting=true;settings.enabled=false;
    step(1000,0);step(2000,0);
    sample_at=wallbox_at=last_sent_current_at=3000;actual_at=1000;phase_step_locked(3000);
    assert(phase_zero_at==0&&relay_on[0]);
    puts("PASS: real phase state machine, evcc/OTA restart parking, uninterrupted zero interlock, cached and pre-stop measurements, idle PV phase choice and feedback fault");
}
