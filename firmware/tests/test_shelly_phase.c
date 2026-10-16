#include "ems_core.h"
#include "shelly_phase.h"
#include <assert.h>
#include <stdio.h>
#include <string.h>
#include <math.h>

static settings_t settings;
typedef enum {PHASE_READY,PHASE_STOPPING,PHASE_MOVING,PHASE_SETTLING,PHASE_FAULT} phase_state_t;
static phase_state_t phase_state;
static uint8_t phase_goal,phase_candidate;
static int64_t phase_candidate_at,phase_stop_at,phase_zero_at,phase_move_at,phase_hold_until,phase_attempt_at,phase_no_load_since;
static bool phase_startup,phase_idle_inhibit,phase_gpio_ready=true,restarting,ota_in_progress,wifi_online=true,relay_on[1];
static float actual_a[3];
static int64_t actual_at,meter_at,wallbox_at,last_sent_current_at;
static struct {bool configured,enabled,preparing;unsigned phases;} evcc_control;
static struct {float target_a;} pv_control;
static current_calibration_t manual_filter;
static unsigned pulses;
static int64_t wallbox_meter_ttl(void){return 5000;}
static void phase_mqtt_guard_locked(void){}
static bool phase_mqtt_remote(void){return false;}
static bool phase_output_link_locked(void){return false;}
static bool phase_mqtt_motion_complete_locked(uint8_t goal,int64_t now){(void)goal;(void)now;return false;}
static unsigned evcc_desired_phases(const void *unused,unsigned active){(void)unused;return evcc_control.enabled?evcc_control.phases:active;}
static float phase_surplus_w_locked(int64_t now){(void)now;return 3300;}
static bool phase_test_active;static bool phase_config_saving,reboot_required;static uint8_t phase_test_goal;
static void phase_test_end_locked(void){phase_test_active=false;}
#include "phase_shelly_runtime.inc"
static unsigned gpio_reads;
static int64_t now_ms(void){return meter_at;}
#define PHASE_FEEDBACK_GPIO settings.phase_feedback_pin
#define PHASE_FEEDBACK_SECOND_GPIO settings.phase_feedback_second_pin
/* Deliberately contradict the Shelly's three-phase contacts. */
static int gpio_get_level(int pin){gpio_reads++;return pin==PHASE_FEEDBACK_GPIO?1:0;}
#include "phase_observed_actual.inc"
static bool phase_actuator_begin_locked(uint8_t goal,int64_t now){return phase_shelly_begin_locked(goal,now);}
#include "phase_controller.inc"

static shelly_phase_feedback_t feedback(unsigned phases,bool output,uint32_t uptime){
    return (shelly_phase_feedback_t){.output=output,.no_closed=phases==3,.nc_closed=phases==1,.phases=phases,.uptime=uptime,.has_uptime=true};
}
static void fixture(unsigned phases){
    settings_defaults(&settings);settings.phase_actuator_type=1;settings.phase_feedback_count=2;settings.phase_feedback_closed_is_single=false;settings.phase_shelly_pulse=true;strcpy(settings.phase_shelly_host,"192.168.1.88");
    settings.xemex_coils=3;settings.phase_switch_enabled=settings.relay_board_enabled=settings.phase_feedback_enabled=true;
    settings.relay1_mode=3;settings.charge_phases=phases;settings.manual_phases=phases;
    settings.mode=MODE_MANUAL;settings.enabled=true;
    phase_state=PHASE_READY;phase_goal=phase_candidate=phases;relay_on[0]=phases==1;
    phase_candidate_at=phase_stop_at=phase_zero_at=phase_move_at=phase_hold_until=phase_attempt_at=phase_no_load_since=0;
    phase_startup=phase_idle_inhibit=restarting=ota_in_progress=false;phase_gpio_ready=wifi_online=true;
    phase_shelly_at=phase_shelly_config_at=1000;phase_shelly_command_at=0;phase_shelly_config_ok=true;
    phase_shelly_pending=phase_shelly_uncertain=phase_shelly_restart_prepared=phase_shelly_have_uptime=false;
    phase_shelly_uptime=0;phase_shelly_feedback_value=feedback(phases,false,100);pulses=0;
    memset(&evcc_control,0,sizeof(evcc_control));memset(&pv_control,0,sizeof(pv_control));
}
static void step(int64_t now,float a1,float a2,unsigned observed,bool output){
    actual_at=meter_at=wallbox_at=last_sent_current_at=now;actual_a[0]=a1;actual_a[1]=a2;actual_a[2]=0;
    shelly_phase_feedback_t status=feedback(observed,output,100+(uint32_t)(now/1000));
    assert(phase_shelly_receive_locked(&status,true,now));phase_step_locked(now);
    if(phase_shelly_take_pulse_locked(now)){
        assert(a1<ems_param(EP_PHASE_ZERO_A)&&a2<ems_param(EP_PHASE_ZERO_A));
        pulses++;phase_shelly_ack_locked(true);
    }
}
static const char *safe_config="{\"switch:0\":{\"id\":0,\"in_mode\":\"detached\",\"initial_state\":\"off\",\"auto_on\":false,\"auto_off\":true,\"auto_off_delay\":0.5},\"input:0\":{\"id\":0,\"type\":\"switch\",\"enable\":true,\"invert\":false},\"input:1\":{\"id\":1,\"type\":\"switch\",\"enable\":true,\"invert\":false}}";
int main(void){
    shelly_phase_identity_t identity;
    assert(shelly_phase_identity("{\"id\":\"shellypro1-abcd\",\"gen\":2,\"model\":\"SPSW-001XE16EU\",\"auth_en\":true}",&identity));
    assert(identity.pro1&&identity.protected_device);
    assert(shelly_phase_identity("{\"id\":\"shelly1g4-abcd\",\"gen\":4,\"model\":\"S4SW\",\"auth_en\":false}",&identity)&&!identity.pro1);
    assert(!shelly_phase_identity("{\"id\":\"other\",\"gen\":2,\"model\":\"SPSW\",\"auth_en\":false}",&identity));
    assert(!shelly_phase_identity("{\"id\":\"shellypro1-abcd\",\"gen\":2,\"gen\":4,\"model\":\"SPSW\",\"auth_en\":false}",&identity));
    assert(shelly_phase_safe_config(safe_config));
    char config[1024];strcpy(config,safe_config);char *auto_on=strstr(config,"\"auto_on\":false");memcpy(auto_on+10,"true ",5);assert(!shelly_phase_safe_config(config));
    assert(!shelly_phase_safe_config("{}"));
    assert(shelly_phase_endpoint_valid("192.168.1.88",64)&&shelly_phase_endpoint_valid("shellypro1.local",64));
    assert(!shelly_phase_endpoint_valid("http://host",64)&&!shelly_phase_endpoint_valid("x/rpc/Switch.Set",64));
    char path[80];assert(shelly_phase_pulse_path(path,sizeof(path))&&strstr(path,"toggle_after=0.5"));
    assert(shelly_phase_pulse_ack("{\"was_on\":false}"));
    assert(!shelly_phase_pulse_ack("{\"was_on\":true}"));
    assert(!shelly_phase_pulse_ack("{\"was_on\":false,\"was_on\":true}"));
    assert(!shelly_phase_pulse_ack("{\"was_on\":false} trailing"));
    assert(!shelly_phase_pulse_ack("{}"));
    const char *status="{\"switch:0\":{\"id\":0,\"output\":false},\"input:0\":{\"id\":0,\"state\":true},\"input:1\":{\"id\":1,\"state\":false},\"sys\":{\"uptime\":100}}";
    shelly_phase_feedback_t parsed;
    assert(shelly_phase_feedback(status,&parsed)&&parsed.phases==3&&!parsed.output&&parsed.uptime==100);
    assert(shelly_phase_feedback("{\"switch:0\":{\"id\":0,\"output\":false},\"input:0\":{\"id\":0,\"state\":false},\"input:1\":{\"id\":1,\"state\":false},\"sys\":{\"uptime\":100}}",&parsed)&&parsed.phases==0);
    assert(shelly_phase_feedback("{\"switch:0\":{\"id\":0,\"output\":false},\"input:0\":{\"id\":0,\"state\":false},\"input:1\":{\"id\":1,\"state\":true},\"sys\":{\"uptime\":100}}",&parsed)&&parsed.phases==1);
    assert(!shelly_phase_feedback("{\"switch:0\":{\"id\":0,\"output\":false},\"input:0\":{\"id\":0,\"state\":true},\"sys\":{\"uptime\":100}}",&parsed));
    assert(!shelly_phase_feedback("{\"switch:0\":{\"id\":0,\"output\":false},\"input:0\":{\"id\":0,\"state\":true,\"errors\":[\"invalid\"]},\"input:1\":{\"id\":1,\"state\":false},\"sys\":{\"uptime\":100}}",&parsed));
    assert(!shelly_phase_feedback("{\"switch:0\":{\"id\":0,\"output\":false},\"input:0\":{\"id\":0,\"state\":1},\"input:1\":{\"id\":1,\"state\":false},\"sys\":{\"uptime\":100}}",&parsed));

    fixture(3);assert(settings_valid(&settings));
    assert(settings_shell_pin_available(&settings,12,true)&&settings_shell_pin_available(&settings,13,true));
    settings.phase_feedback_enabled=false;assert(!settings_valid(&settings));settings.phase_feedback_enabled=true;
    settings.xemex_coils=1;assert(!settings_valid(&settings));settings.xemex_coils=3;
    settings_t old=settings,migrated;old.version=27;
    size_t old_length=(offsetof(settings_t,phase_actuator_type)+3)&~(size_t)3;
    assert(settings_decode(&old,old_length,&migrated)&&migrated.phase_actuator_type==0&&!migrated.phase_shelly_host[0]);
    settings_t after=settings;strcpy(after.phase_shelly_host,"192.168.1.89");assert(!settings_apply_live(&settings,&settings,&after));
    strcpy(after.phase_shelly_password,"synthetic-test-password");
    assert(settings_decode(&after,sizeof(after),&migrated)&&migrated.phase_actuator_type==1&&migrated.phase_feedback_enabled);
    assert(!strcmp(migrated.phase_shelly_host,"192.168.1.89")&&!strcmp(migrated.phase_shelly_password,"synthetic-test-password"));

    /* Each change has a fresh zero hold; no output change until all phases are zero. */
    fixture(3);settings.manual_phases=1;
    step(1000,10,0,3,false);step(2000,10,0,3,false);assert(phase_state==PHASE_STOPPING);
    step(3000,0,2,3,false);assert(!phase_zero_at&&!pulses);
    for(int64_t t=4000;t<12000;t+=1000)step(t,0,0,3,false);
    assert(!pulses);step(12000,0,0,3,false);assert(pulses==1&&phase_state==PHASE_MOVING);
    step(12250,0,0,1,true);assert(phase_state==PHASE_MOVING);
    step(13000,0,0,1,false);assert(settings.charge_phases==1&&phase_state==PHASE_SETTLING);
    step(18000,0,0,1,false);assert(phase_state==PHASE_READY&&pulses==1);
    step(19000,8,0,1,false);assert(pulses==1);
    settings.manual_phases=3;
    step(20000,8,0,1,false);step(21000,8,0,1,false);assert(phase_state==PHASE_STOPPING);
    step(22000,0,0,1,false);step(23000,NAN,0,1,false);assert(!phase_zero_at&&pulses==1);
    step(24000,-1,0,1,false);assert(!phase_zero_at&&pulses==1);
    for(int64_t t=25000;t<=33000;t+=1000)step(t,0,0,1,false);
    assert(pulses==2&&phase_state==PHASE_MOVING);
    step(34000,0,0,3,false);step(39000,0,0,3,false);
    assert(phase_state==PHASE_READY&&settings.charge_phases==3&&pulses==2);

    /* Delayed WLAN and HTTP startup must not latch a fault before the first
       contact reading. It also must not issue a blind startup pulse. */
    fixture(3);settings.enabled=false;phase_startup=true;phase_state=PHASE_MOVING;phase_move_at=1000;
    phase_shelly_config_ok=false;wifi_online=false;
    phase_shelly_lost_locked();phase_step_locked(2000);assert(phase_state==PHASE_MOVING&&!pulses);
    wifi_online=true;phase_shelly_receive_locked(NULL,false,10000);phase_step_locked(10000);
    assert(phase_state==PHASE_MOVING&&!pulses);
    phase_shelly_config_ok=true;step(12000,0,0,1,false);step(17000,0,0,1,false);
    assert(phase_state==PHASE_READY&&settings.charge_phases==1&&!pulses);
    fixture(3);settings.enabled=false;phase_startup=true;phase_state=PHASE_MOVING;phase_move_at=1000;
    phase_shelly_config_ok=false;phase_shelly_lost_locked();phase_step_locked(62000);
    assert(phase_state==PHASE_FAULT&&!pulses);

    /* ESP boot reads the retained mechanical position instead of toggling. */
    fixture(3);settings.enabled=false;phase_startup=true;phase_state=PHASE_MOVING;phase_move_at=1000;
    step(1000,0,0,1,false);assert(settings.charge_phases==1&&phase_goal==1&&!pulses);
    step(6000,0,0,1,false);assert(phase_state==PHASE_READY&&!pulses);
    restarting=true;step(7000,0,0,1,false);assert(phase_state==PHASE_STOPPING&&phase_goal==1);
    for(int64_t t=8000;t<=16000;t+=1000)step(t,0,0,1,false);
    step(17000,0,0,1,false);step(22000,0,0,1,false);
    assert(phase_state==PHASE_READY&&settings.charge_phases==1&&!pulses);

    fixture(3);step(1000,0,0,0,false);assert(phase_state==PHASE_FAULT&&!pulses);
    fixture(3);phase_shelly_receive_locked(NULL,false,2000);assert(phase_state==PHASE_FAULT&&!phase_shelly_at);
    fixture(3);step(1000,0,0,3,false);shelly_phase_feedback_t reset=feedback(3,false,1);
    assert(!phase_shelly_receive_locked(&reset,true,2000)&&phase_shelly_uncertain&&phase_state==PHASE_FAULT);
    fixture(3);phase_shelly_ack_locked(false);assert(phase_state==PHASE_FAULT&&!phase_shelly_begin_locked(1,1000));
    fixture(3);phase_shelly_at=1000;phase_shelly_guard_locked(7000);assert(phase_state==PHASE_FAULT);
    fixture(3);phase_shelly_at=1000;phase_shelly_pending=true;phase_shelly_goal=phase_goal=1;
    phase_state=PHASE_MOVING;restarting=true;phase_shelly_guard_locked(1100);assert(!phase_shelly_pending&&phase_goal==3&&!pulses);

    /* Complementary mechanical contacts and known dry-contact device profiles. */
    assert(phase_feedback_contacts(true,false,2,false)==3);
    assert(phase_feedback_contacts(false,true,2,false)==1);
    assert(phase_feedback_contacts(false,false,2,false)==0);
    assert(phase_feedback_contacts(true,true,2,false)==0);
    assert(phase_feedback_contacts(true,false,1,true)==1);
    fixture(3);settings.phase_actuator_type=0;settings.phase_feedback_count=2;
    assert(!settings_shell_pin_available(&settings,14,true));
    settings.wallbox_tx_pin=14;assert(!settings_valid(&settings));
    assert(shelly_phase_identity("{\"id\":\"shellypro2-abcd\",\"gen\":2,\"model\":\"SPSW\",\"auth_en\":false}",&identity)&&identity.profile==1&&identity.inputs==2);
    assert(shelly_phase_identity("{\"id\":\"shellyplus1-abcd\",\"gen\":2,\"model\":\"SNSW\",\"auth_en\":false}",&identity)&&identity.profile==2&&identity.inputs==1);
    assert(shelly_phase_identity("{\"id\":\"shelly1g3-abcd\",\"gen\":3,\"model\":\"S3SW\",\"auth_en\":false}",&identity)&&identity.profile==3&&identity.inputs==1);
    assert(shelly_phase_identity("{\"id\":\"shelly1g4-abcd\",\"gen\":4,\"model\":\"S4SW\",\"auth_en\":false}",&identity)&&identity.profile==4&&identity.inputs==1);
    static const struct {const char *id;unsigned generation,profile,inputs;} additional[]={
        {"shellypro1pm-abcd",2,5,2},{"shellypro2pm-abcd",2,6,2},
        {"shellyplus2pm-abcd",2,7,2},{"shellypro3-abcd",2,8,3},
        {"shellypro4pm-abcd",2,9,4},{"shellyplus1pm-abcd",2,10,1},
        {"shelly1pmg3-abcd",3,11,1},{"shelly1minig4-abcd",4,12,1}
    };
    char identity_json[160];
    for(unsigned i=0;i<sizeof(additional)/sizeof(additional[0]);i++){
        snprintf(identity_json,sizeof(identity_json),"{\"id\":\"%s\",\"gen\":%u,\"model\":\"230V\",\"auth_en\":false}",additional[i].id,additional[i].generation);
        assert(shelly_phase_identity(identity_json,&identity)&&identity.profile==additional[i].profile&&identity.inputs==additional[i].inputs);
        assert(shelly_phase_profile_inputs(additional[i].profile)==additional[i].inputs);
    }
    assert(!shelly_phase_profile_inputs(13));
    assert(shelly_phase_identity("{\"id\":\"shellypro3-abcd\",\"gen\":3,\"model\":\"wrong\",\"auth_en\":false}",&identity)&&identity.profile==255);
    const char *one="{\"switch:0\":{\"id\":0,\"output\":true},\"input:0\":{\"id\":0,\"state\":true},\"sys\":{\"uptime\":100}}";
    assert(shelly_phase_feedback_for(one,&parsed,1,true)&&parsed.phases==1&&parsed.has_uptime);
    assert(!shelly_phase_feedback_for(one,&parsed,2,true));
    const char *continuous="{\"switch:0\":{\"id\":0,\"in_mode\":\"detached\",\"initial_state\":\"off\",\"auto_on\":false,\"auto_off\":false},\"input:0\":{\"id\":0,\"type\":\"switch\",\"enable\":true,\"invert\":false}}";
    assert(shelly_phase_config_for(continuous,1,false));
    assert(!shelly_phase_config_for(continuous,2,false)&&!shelly_phase_config_for(continuous,1,true));
    fixture(3);settings.phase_shelly_model=5;settings.phase_feedback_count=2;assert(settings_valid(&settings));
    settings.phase_shelly_model=10;assert(!settings_valid(&settings));settings.phase_feedback_count=1;settings.phase_shelly_pulse=false;assert(settings_valid(&settings));
    settings.phase_actuator_type=3;settings.phase_shelly_model=12;assert(settings_valid(&settings));
    for(unsigned transport=1;transport<=3;transport+=2)for(unsigned polarity=0;polarity<2;polarity++){
        fixture(3);settings.phase_actuator_type=transport;settings.phase_shelly_pulse=false;
        settings.phase_shelly_model=transport==3?4:2;settings.phase_feedback_count=1;
        settings.phase_shelly_true_three_phase=polarity;phase_shelly_feedback_value.output=polarity;
        assert(settings_valid(&settings));settings.manual_phases=1;
        step(1000,10,0,3,polarity);step(2000,10,0,3,polarity);step(3000,0,2,3,polarity);assert(!pulses);
        for(int64_t t=4000;t<=12000;t+=1000)step(t,0,0,3,polarity);
        assert(pulses==1&&phase_state==PHASE_MOVING);
        step(13000,0,0,3,!polarity);assert(phase_state==PHASE_MOVING);
        step(14000,0,0,1,!polarity);step(19000,0,0,1,!polarity);
        assert(phase_state==PHASE_READY&&settings.charge_phases==1&&pulses==1);
        step(20000,8,0,0,!polarity);assert(phase_state==PHASE_FAULT&&pulses==1);
    }
    /* Legacy feedback becomes the fixed contact-1=three-phase assignment. */
    fixture(3);old=settings;old.version=31;old.phase_actuator_type=0;old.phase_feedback_enabled=false;
    old.phase_feedback_closed_is_single=true;
    size_t v31_length=(offsetof(settings_t,phase_feedback_count)+3)&~(size_t)3;
    assert(settings_decode(&old,v31_length,&migrated)&&migrated.phase_feedback_enabled&&migrated.phase_feedback_count==1&&!migrated.phase_feedback_closed_is_single);
    old.phase_actuator_type=1;old.phase_feedback_enabled=true;
    assert(settings_decode(&old,v31_length,&migrated)&&migrated.phase_feedback_count==2&&migrated.phase_shelly_pulse&&!migrated.phase_feedback_closed_is_single);
    for(unsigned transport=1;transport<=3;transport+=2){
        fixture(3);settings.phase_actuator_type=transport;meter_at=1000;gpio_reads=0;
        assert(phase_observed()==3&&gpio_reads==0);
        phase_shelly_config_ok=false;assert(phase_observed()==0&&gpio_reads==0);
        phase_shelly_config_ok=true;meter_at=1000+SHELLY_PHASE_TTL_MS+1;
        assert(phase_observed()==0&&gpio_reads==0);
        settings.phase_actuator_type=0;assert(phase_observed()==1&&gpio_reads==2);
    }
    puts("PASS: Shelly Pro 1 discovery, guarded config, dual ABB contacts, timed pulse, recovery, missing/stale feedback; real HTTP/Modbus dispatcher never reads GPIO or falls back to it");
}
