#include "ems_core.h"
#include "evcc_bridge.h"
#include "cJSON.h"
#include <assert.h>
#include <math.h>
#include <stdio.h>
#include <string.h>
enum {PHASE_READY,PHASE_STOPPING,PHASE_MOVING,PHASE_SETTLING,PHASE_FAULT};
static settings_t settings;
static bool phase_gpio_ready=true,phase_idle_inhibit,phase_startup,restarting,ota_in_progress;
static void phase_mqtt_guard_locked(void){}
static bool phase_mqtt_remote(void){return false;}
static bool phase_output_link_locked(void){return false;}
static bool phase_mqtt_motion_complete_locked(uint8_t goal,int64_t now){(void)goal;(void)now;return false;}
static unsigned phase_state,observed=3;
static uint8_t phase_goal=3,phase_candidate=3;
static int64_t phase_candidate_at,phase_stop_at,phase_zero_at,phase_move_at,phase_hold_until,phase_attempt_at,phase_no_load_since;
static int64_t actual_at,meter_at,wallbox_at,last_sent_current_at,sample_at,relay_changed_at;
static float actual_a[3],solar_w=3300;
static bool relay_on[1],fresh_meter=true;
static pv_control_t pv_control;
static manual_report_filter_t manual_filter;
static evcc_control_t evcc_control;
static int64_t meter_ttl=5000;
static int64_t wallbox_meter_ttl(void){return meter_ttl;}
static unsigned phase_observed(void){return observed;}
static float phase_surplus_w_locked(int64_t now){(void)now;return solar_w;}
static void relay_set(unsigned index,bool on){
    assert(index==0 && actual_a[0]<1 && actual_a[1]<1 && actual_a[2]<1);
    assert(phase_zero_at>0 && sample_at-phase_zero_at>=ems_param_ms(EP_PHASE_ZERO));
    relay_on[0]=on;observed=on?1:3;relay_changed_at=sample_at;
}
static void phase_shelly_guard_locked(int64_t now){(void)now;}
static bool phase_shelly_remote(void){return false;}
static bool phase_shelly_motion_complete_locked(unsigned goal,int64_t now){(void)goal;(void)now;return false;}
static bool phase_actuator_begin_locked(uint8_t goal,int64_t now){(void)now;relay_set(0,goal==1);return true;}
static bool phase_test_active;static bool phase_config_saving,reboot_required;static uint8_t phase_test_goal;
static void phase_test_end_locked(void){phase_test_active=false;}
#include "phase_controller.inc"
static bool terms_accepted=true,evcc_configuring,phase_mqtt_saving,wallbox_ready=true;
static charge_guard_t charge_guard;
static struct {bool active;} charge_plan;
static struct {bool stable;} evu_contact;
enum {PLAN_CLOCK=1};
static unsigned plan_phase;
static evcc_shell_t evcc_shell;
static int64_t evcc_shell_at;
static bool restart_pending(void){return false;}
static bool unsupported_meter(void){return false;}
static bool feedback_ready(int64_t now){(void)now;return true;}
static bool pv_fresh_locked(int64_t now){(void)now;return true;}
static bool house_currents_ready_locked(int64_t now){(void)now;return true;}
typedef int esp_err_t;
typedef struct {const char *body;int status;} httpd_req_t;
#define ESP_OK 0
static unsigned locks;
#define LOCK() assert(locks++==0)
#define UNLOCK() assert(--locks==0)
static int64_t now_ms(void){return sample_at;}
static bool authorized(httpd_req_t *req){(void)req;return true;}
static cJSON *read_json(httpd_req_t *req){return cJSON_Parse(req->body);}
static void httpd_resp_set_status(httpd_req_t *req,const char *status){(void)status;req->status=409;}
static int httpd_resp_sendstr(httpd_req_t *req,const char *body){(void)req;(void)body;return ESP_OK;}
static int ok_json(httpd_req_t *req){req->status=200;return ESP_OK;}
static void event_locked(const char *event){(void)event;}
#include "phase_control_actual.inc"
static void fixture(unsigned phases){
    settings_defaults(&settings);settings.phase_switch_enabled=settings.relay_board_enabled=settings.phase_feedback_enabled=true;
    settings.charge_phases=phases;settings.enabled=settings.zero_feed_enabled=true;settings.mode=MODE_PV;
    phase_state=PHASE_READY;phase_candidate=phase_goal=observed=phases;relay_on[0]=phases==1;
    phase_candidate_at=phase_stop_at=phase_zero_at=phase_move_at=phase_hold_until=phase_attempt_at=phase_no_load_since=relay_changed_at=0;
    phase_idle_inhibit=phase_startup=restarting=ota_in_progress=false;fresh_meter=phase_gpio_ready=true;meter_ttl=5000;
    memset(&evcc_control,0,sizeof(evcc_control));memset(&pv_control,0,sizeof(pv_control));memset(&manual_filter,0,sizeof(manual_filter));solar_w=3300;
    evcc_shell=(evcc_shell_t){.state='C',.evcc_usable=true};evcc_shell_at=1000;
}
static void step(int64_t now,float amps){
    sample_at=now;actual_at=fresh_meter?now:0;meter_at=wallbox_at=last_sent_current_at=now;
    evcc_shell_at=now;
    actual_a[0]=amps;actual_a[1]=actual_a[2]=0;phase_step_locked(now);
}
int main(void){
    /* An evcc attempt with no current needs an explicit local release. Polls,
       automatic phase commands and enable retries cannot clear that lock. */
    fixture(1);settings.mode=MODE_MANUAL;settings.manual_phases=1;
    evcc_configure(&evcc_control,true);
    assert(evcc_set_current(&evcc_control,10,settings.min_charge_a,settings.max_charge_a,1000));
    assert(evcc_set_phases(&evcc_control,1,true,true,1000));
    assert(evcc_set_enabled(&evcc_control,true,true,1000));phase_attempt_at=1000;
    int64_t idle_end=ems_param_ms(EP_PHASE_IDLE)+30000;
    for(int64_t t=1000;t<=idle_end;t+=1000){evcc_touch(&evcc_control,t);step(t,0);}
    assert(phase_idle_inhibit&&evcc_control.inhibited&&!evcc_control.enabled);
    assert(!settings.enabled&&phase_state==PHASE_READY&&settings.charge_phases==3);
    for(unsigned retry=0;retry<3;retry++) {
        evcc_touch(&evcc_control,idle_end);
        assert(evcc_set_current(&evcc_control,10,settings.min_charge_a,settings.max_charge_a,idle_end));
        assert(!evcc_set_enabled(&evcc_control,true,evcc_ready_locked(idle_end),idle_end));
        assert(!evcc_set_phases(&evcc_control,1,true,evcc_ready_locked(idle_end),idle_end));
        assert(unlatched_target_locked(idle_end)==0&&phase_idle_inhibit);
    }
    httpd_req_t release={.body="{\"release\":true}"};
    assert(evcc_release_post(&release)==ESP_OK&&release.status==200&&!locks);
    assert(!phase_idle_inhibit&&!evcc_control.inhibited&&!phase_attempt_at&&!phase_no_load_since);
    assert(!settings.enabled&&!evcc_control.enabled&&unlatched_target_locked(idle_end)==0);
    assert(evcc_ready_locked(idle_end));
    assert(evcc_set_enabled(&evcc_control,true,evcc_ready_locked(idle_end),idle_end));

    /* Live maximum changes must reach the Shell before the next controller
       tick, including a reduction smaller than the PV deadband. */
    fixture(3);step(1000,16);pv_control.target_a=16;pv_control.last_at=1000;
    settings.max_charge_a=15.9f;
    float limited=unlatched_target_locked(1000),report[3];
    assert(fabsf(limited-15.9f)<.001f);
    control_report(&settings,actual_a,limited,0,false,report);
    assert(report[0]<settings.grid_limit_a+settings.max_charge_a+1);
    assert(report[1]<settings.grid_limit_a+settings.max_charge_a+1);

    /* A failed one-phase attempt parks the coil but must never start charging
       on three phases while a one-phase/manual or PV request remains active. */
    for(unsigned scenario=0;scenario<2;scenario++) {
        control_mode_t mode=scenario?MODE_PV:MODE_MANUAL;
        fixture(1);settings.mode=mode;settings.manual_phases=1;settings.manual_current_a=10;
        settings.pv_surplus_a=12;phase_attempt_at=1000;
        int64_t finished=ems_param_ms(EP_PHASE_IDLE)+30000;
        for(int64_t t=1000;t<=finished;t+=1000)step(t,0);
        assert(phase_state==PHASE_READY&&settings.charge_phases==3&&!relay_on[0]);
        assert(phase_idle_inhibit&&settings.enabled);
        pv_control.target_a=10;pv_control.last_at=finished;
        for(unsigned request_mode=MODE_MANUAL;request_mode<=MODE_PV;request_mode++) {
            settings.mode=request_mode;
            assert(unlatched_target_locked(finished)==0);
            assert(!pv_permitted_locked(finished));
        }
        settings.mode=mode;
        settings_t next=settings;
        idle_resume_command(next,false);assert(phase_idle_inhibit); /* polling/repeated request */
        next.enabled=false;idle_resume_command(next,true);assert(phase_idle_inhibit); /* stop */
        next=settings;idle_resume_command(next,true);assert(!phase_idle_inhibit); /* explicit Start */
        if(mode==MODE_PV)assert(pv_permitted_locked(finished));
        else assert(unlatched_target_locked(finished)>0);
    }
    /* Existing explicit changes rearm the attempt; an unchanged command does not. */
    fixture(3);phase_idle_inhibit=true;settings_t next=settings;
    next.mode=MODE_MANUAL;idle_resume_command(next,false);assert(!phase_idle_inhibit);
    phase_idle_inhibit=true;next=settings;next.manual_phases=1;
    idle_resume_command(next,false);assert(!phase_idle_inhibit);
    phase_idle_inhibit=true;next=settings;next.manual_current_a+=1;
    idle_resume_command(next,false);assert(!phase_idle_inhibit);
    phase_idle_inhibit=true;settings.enabled=false;next=settings;next.enabled=true;
    idle_resume_command(next,false);assert(!phase_idle_inhibit);
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
    puts("PASS: real phase/charge gates, idle lock in every mode, evcc retries remain locked until explicit local release, PV maximum, restart parking, zero interlock and feedback fault");
}
