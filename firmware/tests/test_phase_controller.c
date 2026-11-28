#include "ems_core.h"
#include "meter_hold.h"
#include "evcc_bridge.h"
#include "cJSON.h"
#include <assert.h>
#include <math.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>
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
static bool phase_shelly_rechecking,phase_shelly_uncertain;
static int64_t phase_shelly_recheck_at,phase_shelly_at;
static bool phase_shelly_remote(void){return settings.phase_actuator_type==1;}
static bool phase_shelly_motion_complete_locked(unsigned goal,int64_t now){(void)goal;(void)now;return false;}
static bool phase_actuator_begin_locked(uint8_t goal,int64_t now){(void)now;relay_set(0,goal==1);return true;}
static bool phase_test_active;static bool phase_config_saving,reboot_required;static uint8_t phase_test_goal;
static void phase_test_end_locked(void){phase_test_active=false;}
static float vehicle_phase_surplus_locked(float watts){return watts;}
#include "phase_voltage.h"
static phase_voltage_proof_t phase_voltage;
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
static bool network_meter;
static bool network_wallbox_meter(void){return network_meter;}
static bool meter_ready=true;
static struct {bool running;struct {bool fault;} control;} http_regulator;
static bool wired_backup_armed,wifi_online=true;
static int64_t shell_limits_at;
static unsigned shell_limits_code;
static float shell_connector_max_a,shell_grid_a;
static bool phase_shelly_wrong_position_locked(int64_t now,unsigned expected){(void)now;(void)expected;return false;}
static bool http_regulation_ready_locked(int64_t now){(void)now;return false;}
static float vehicle_limit_current_locked(float current){return current;}
#include "meter_hold_runtime.inc"
static bool pv_available=true,house_available=true,backup_position;
static int64_t house_power_at;
static int64_t house_meter_ttl(void){return 5000;}
static bool phase_shelly_backup_position_locked(int64_t now){(void)now;return backup_position;}
static bool pv_fresh_locked(int64_t now){(void)now;return pv_available;}
static bool house_currents_ready_locked(int64_t now){(void)now;return house_available;}
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
    settings.basic_mode=false;
    settings.grid_backup_enabled=settings.shell_native_fallback=false;
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
static void manual_phase_admission(void){
    /* The API commits manual_phases before either candidate-debounce tick.
       Exercise the real target function and phase worker through both directions. */
    for(unsigned previous=1;previous<=3;previous+=2){
        unsigned requested=previous==1?3:1;
        fixture(previous);settings.mode=MODE_MANUAL;settings.manual_phases=previous;
        settings.fixed_charge_phases=3;settings.manual_current_a=10;step(1000,0);
        assert(unlatched_target_locked(1000)==10);
        settings.manual_phases=requested;
        assert(phase_state==PHASE_READY&&unlatched_target_locked(1000)==0);
        float report[3];control_report(&settings,actual_a,target_locked(1000),0,false,report);
        for(unsigned p=0;p<3;p++)assert(report[p]==(p<previous?settings.grid_limit_a+settings.max_charge_a+1:0));
        step(2000,0);assert(phase_state==PHASE_READY&&phase_candidate==requested&&unlatched_target_locked(2000)==0);
        step(3000,0);assert(phase_state==PHASE_STOPPING&&phase_goal==requested&&unlatched_target_locked(3000)==0);
        for(int64_t at=4000;at<=18000;at+=1000){
            step(at,0);
            if(phase_state!=PHASE_READY)assert(unlatched_target_locked(at)==0);
        }
        assert(phase_state==PHASE_READY&&settings.charge_phases==requested&&unlatched_target_locked(18000)==10);
    }
    fixture(3);settings.mode=MODE_MANUAL;settings.manual_current_a=10;settings.manual_phases=1;
    settings.phase_switch_enabled=false;step(1000,0);assert(unlatched_target_locked(1000)==10);
    fixture(3);settings.manual_phases=1;step(1000,0);pv_control.target_a=10;pv_control.last_at=1000;
    assert(unlatched_target_locked(1000)==10); /* PV retains its own phase policy. */
    fixture(3);settings.mode=MODE_MANUAL;settings.manual_current_a=10;settings.manual_phases=1;
    evcc_configure(&evcc_control,true);
    assert(evcc_set_current(&evcc_control,10,settings.min_charge_a,settings.max_charge_a,1000));
    assert(evcc_set_phases(&evcc_control,3,true,true,1000));
    assert(evcc_set_enabled(&evcc_control,true,true,1000));step(1000,0);
    assert(unlatched_target_locked(1000)==10); /* Manual preset does not override evcc. */
    assert(evcc_set_phases(&evcc_control,1,true,true,1000));
    assert(unlatched_target_locked(1000)==0); /* Existing evcc mismatch gate remains. */
}
static void wired_router_outage(bool stopped,bool switch_fault,bool wifi_connected,bool realtime){
    fixture(3);settings.mode=MODE_MANUAL;settings.manual_phases=3;settings.manual_current_a=10;
    settings.phase_actuator_type=1;settings.grid_backup_enabled=true;settings.shell_limits_auto=true;
    shell_limits_at=wallbox_at=actual_at=meter_at=1000;shell_limits_code=200;shell_connector_max_a=settings.max_charge_a;
    shell_grid_a=settings.grid_limit_a;wired_backup_armed=false;arm_wired_backup_locked(2000);
    assert(wired_backup_armed);wifi_online=wifi_connected;phase_shelly_rechecking=true;phase_state=PHASE_SETTLING;
    if(!wifi_connected)actual_at=meter_at=0;
    house_power_at=evcc_shell_at=0;
    uint8_t req[8]={1,3,0x50,0x0c,0,6},reply[17];uint16_t crc=modbus_crc(req,6);req[6]=(uint8_t)crc;req[7]=crc>>8;
    for(int64_t t=2000;t<152000;t+=1000){
        wallbox_at=t; /* the local RS485 bus is the only live connection */
        if(t==62000&&stopped)settings.enabled=false;
        if(t==62000&&switch_fault)phase_state=PHASE_FAULT;
        float target=target_locked(t),reported[3],decoded[3];
        bool blocked=t>=62000&&(stopped||switch_fault);
        assert(target==(blocked?0:settings.max_charge_a));
        control_report(&settings,actual_a,target,0,false,reported);
        if(wired_backup_locked(t))wired_backup_report(&settings,target,reported);
        assert(decode_meter(reply,modbus_reply(req,8,reply,&settings,reported),1,decoded));
        for(unsigned p=0;p<3;p++)assert(decoded[p]==(blocked?settings.grid_limit_a+settings.max_charge_a+1:settings.grid_limit_a-settings.max_charge_a));
        if(realtime)sleep(1);
    }
    if(!stopped&&!switch_fault){
        assert(!wired_backup_locked(180000)); /* wired bus also absent */
        wallbox_at=180000;settings.grid_guard_enabled=true;assert(target_locked(180000)==0);
        settings.grid_guard_enabled=false;settings.evu_input_enabled=true;evu_contact.stable=true;
        assert(target_locked(180000)==0);evu_contact.stable=false;
        shell_connector_max_a=32;assert(target_locked(180000)==0);
        shell_connector_max_a=16;charge_plan.active=true;assert(target_locked(180000)==0);charge_plan.active=false;
        wifi_online=true;phase_shelly_rechecking=false;phase_state=PHASE_READY;
        actual_at=meter_at=wallbox_at=house_power_at=181000;
        assert(!wired_backup_locked(181000)&&target_locked(181000)==10);
    }
    wifi_online=true;phase_shelly_rechecking=false;wired_backup_armed=false;
}
static void meter_stream_stalls(void){
    fixture(3);network_meter=true;settings.mode=MODE_MANUAL;settings.manual_phases=3;
    settings.phase_actuator_type=1;settings.zero_feed_enabled=false;settings.grid_backup_enabled=true;
    settings.shell_limits_auto=true;settings.manual_current_a=10;shell_limits_code=200;
    shell_grid_a=settings.grid_limit_a;shell_connector_max_a=settings.max_charge_a;
    shell_limits_at=actual_at=meter_at=wallbox_at=1000;
    float flow[3]={10,10,10};meter_hold_sample(&wallbox_meter_hold,&settings,true,true,1000,1000,flow,10);
    arm_wired_backup_locked(1000);assert(wired_backup_armed);
    for(int64_t t=1000;t<=151000;t+=1000){
        wallbox_at=t; /* healthy bus and actuator, meter timestamp frozen */
        float target=target_locked(t);
        if(t<6000)assert(target==10);
        else{
            assert(wired_backup_locked(t)&&target==settings.max_charge_a);
            float report[3];wired_backup_report(&settings,target,report);
            for(unsigned p=0;p<3;p++)assert(report[p]==shell_grid_a-shell_connector_max_a);
        }
    }
    actual_at=meter_at=wallbox_at=152000;assert(!wired_backup_locked(152000)&&target_locked(152000)==10);
    memset(&wallbox_meter_hold,0,sizeof(wallbox_meter_hold));network_meter=false;wired_backup_armed=false;
}
static void native_shell_outage(void){
    fixture(3);settings.mode=MODE_MANUAL;settings.manual_phases=3;settings.manual_current_a=10;
    settings.phase_actuator_type=1;settings.grid_backup_enabled=settings.shell_native_fallback=true;
    settings.shell_limits_auto=true;settings.zero_feed_enabled=false;
    shell_connector_max_a=settings.max_charge_a;shell_grid_a=settings.grid_limit_a;shell_limits_code=200;
    wallbox_at=actual_at=meter_at=shell_limits_at=1000;wired_backup_armed=false;
    actual_a[0]=0;arm_wired_backup_locked(1000);assert(!wired_backup_armed);
    actual_a[0]=actual_a[1]=actual_a[2]=10;
    settings.basic_mode=true;arm_wired_backup_locked(1000);assert(!wired_backup_armed);
    settings.basic_mode=false;arm_wired_backup_locked(1000);assert(wired_backup_armed);
    wifi_online=false;meter_at=actual_at=0;
    uint8_t req[8]={1,3,0x50,0x0c,0,6},reply[17];
    uint16_t crc=modbus_crc(req,6);req[6]=(uint8_t)crc;req[7]=crc>>8;
    float currents[3]={32,32,32};
    for(int64_t t=2000;t<152000;t+=1000){
        assert(target_locked(t)==settings.max_charge_a);
        assert(shell_meter_reply_locked(req,reply,currents,t,target_locked(t))==0);
        assert(wallbox_at==1000);
    }
    settings.enabled=false;
    assert(target_locked(152000)==0&&shell_meter_reply_locked(req,reply,currents,152000,0)==17);
    settings.enabled=true;phase_state=PHASE_FAULT;
    assert(target_locked(152000)==0&&shell_meter_reply_locked(req,reply,currents,152000,0)==17);
    phase_state=PHASE_READY;wifi_online=true;meter_at=actual_at=152000;
    assert(target_locked(152000)==10&&shell_meter_reply_locked(req,reply,currents,152000,10)==17);
    wired_backup_armed=false;
    memset(actual_a,0,sizeof(actual_a));
}
int main(int argc,char **argv){
    native_shell_outage();
    puts("PASS: 150 unanswered polls, no fake transmit timestamps, resume on recovery, stop and fault");
    wired_router_outage(false,false,false,argc==2&&!strcmp(argv[1],"--realtime"));
    wired_router_outage(false,false,true,false);
    wired_router_outage(true,false,false,false);wired_router_outage(false,true,false,false);
    meter_stream_stalls();
    puts("PASS: 150 RS485 requests during 150 s without Wi-Fi, HTTP or meter; configured cap, manual stop, switch fault and limits verified");
    fixture(3);settings.manual_phases=3;settings.grid_backup_enabled=true;
    house_power_at=1000;pv_available=false;step(2000,0);
    assert(target_locked(2000)==settings.max_charge_a);
    settings.grid_guard_enabled=true;house_available=false;
    assert(target_locked(2000)<settings.max_charge_a&&target_locked(2000)>0);
    settings.evu_input_enabled=true;evu_contact.stable=true;settings.evu_limit_a=0;
    assert(target_locked(2000)==0);evu_contact.stable=false;
    phase_state=PHASE_FAULT;assert(target_locked(2000)==0);
    phase_state=PHASE_SETTLING;assert(target_locked(2000)==0);
    backup_position=true;assert(target_locked(2000)>0);
    actual_at=0;assert(target_locked(2000)==0);actual_at=2000;
    settings.enabled=false;assert(target_locked(2000)==0);settings.enabled=true;
    backup_position=false;phase_state=PHASE_READY;pv_available=house_available=true;
    assert(target_locked(2000)==0); /* PV data back, no surplus */
    settings.grid_backup_enabled=false;pv_available=false;assert(target_locked(2000)==0);pv_available=true;
    manual_phase_admission();
    fixture(1);settings.mode=MODE_MANUAL;settings.manual_phases=1;settings.enabled=false;
    phase_attempt_at=phase_no_load_since=1000;step(500000,0);
    assert(!phase_idle_inhibit&&!phase_attempt_at&&!phase_no_load_since);
    fixture(1);settings.mode=MODE_MANUAL;settings.manual_phases=1;
    phase_attempt_at=phase_no_load_since=1000;phase_state=PHASE_FAULT;step(500000,0);
    assert(!phase_no_load_since&&!phase_idle_inhibit);
    phase_state=PHASE_READY;step(501000,0);step(502000,0);
    assert(!phase_idle_inhibit&&phase_no_load_since==501000); /* Fault time never consumes the no-current attempt. */
    fixture(3);settings.mode=MODE_MANUAL;settings.manual_current_a=9;
    network_meter=true;actual_at=meter_at=1000;for(unsigned i=0;i<3;i++)actual_a[i]=9;
    meter_hold_sample(&wallbox_meter_hold,&settings,true,true,1000,actual_at,actual_a,9);
    assert(target_locked(20000)==9);settings.manual_current_a=16;assert(target_locked(30000)==9);
    settings.enabled=false;assert(target_locked(30000)==0);settings.enabled=true;
    assert(target_locked(61000)==0);phase_state=PHASE_STOPPING;assert(target_locked(30000)==0);
    network_meter=false;memset(&wallbox_meter_hold,0,sizeof(wallbox_meter_hold));

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
        else assert(unlatched_target_locked(finished)==0); /* Rearmed, but still awaiting the requested physical phase. */
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
    fixture(3);settings.phase_feedback_source=1;phase_goal=observed=1;phase_state=PHASE_MOVING;phase_move_at=10000;
    phase_voltage.at=9500;step(12000,0);assert(phase_state==PHASE_MOVING);
    phase_voltage.at=12500;step(12500,0);assert(phase_state==PHASE_SETTLING&&settings.charge_phases==1);
    puts("PASS: real phase/charge gates, idle lock in every mode, evcc retries remain locked until explicit local release, PV maximum, restart parking, zero interlock and fresh post-command voltage proof");
}
