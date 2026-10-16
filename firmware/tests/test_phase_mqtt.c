#include "ems_core.h"
static uint32_t boot_task_seen[5];
#include <assert.h>
#include <math.h>
#include <stdio.h>
#include <string.h>
enum {PHASE_READY,PHASE_STOPPING,PHASE_MOVING,PHASE_SETTLING,PHASE_FAULT};
static settings_t settings;
static bool phase_mqtt_allowed,phase_mqtt_saving,mqtt_online=true,wifi_online=true,relay_ready,phase_gpio_ready;
static bool phase_idle_inhibit,phase_startup,restarting,ota_in_progress,backup_in_progress,evcc_configuring;
static unsigned phase_state;static uint8_t phase_goal=3,phase_candidate=3;
static int64_t phase_candidate_at,phase_stop_at,phase_zero_at,phase_move_at,phase_hold_until,phase_attempt_at,phase_no_load_since;
static int64_t actual_at,meter_at,wallbox_at,last_sent_current_at,clock_ms;
static float actual_a[3],solar_w=3300;static bool relay_on[1];
static pv_control_t pv_control;static manual_report_filter_t manual_filter;
static struct {bool configured,enabled,preparing;unsigned phases;} evcc_control;
static int64_t wallbox_meter_ttl(void){return 5000;}
static int64_t now_ms(void){return clock_ms;}
static bool phase_test_active;static bool phase_config_saving,reboot_required;static uint8_t phase_test_goal;
static void phase_test_end_locked(void){phase_test_active=false;}
#include "phase_mqtt_runtime.inc"
static unsigned phase_observed(void){return 0;}
static unsigned evcc_desired_phases(const void *unused,unsigned active){(void)unused;return evcc_control.enabled?evcc_control.phases:active;}
static float phase_surplus_w_locked(int64_t now){(void)now;return solar_w;}
static void phase_shelly_guard_locked(int64_t now){(void)now;}
static bool phase_shelly_remote(void){return false;}
static bool phase_shelly_motion_complete_locked(unsigned goal,int64_t now){(void)goal;(void)now;return false;}
static bool phase_actuator_begin_locked(uint8_t goal,int64_t now){return phase_mqtt_begin_locked(goal,now);}
#include "phase_controller.inc"
static int phase_mqtt_send_lock,mqtt_client=1,locks,published,result,revoked_in_publish;
static char topic_sent[128],payload_sent[8];
#define LOCK() assert(locks++==0)
#define UNLOCK() assert(--locks==0)
#define pdTRUE 1
#define ticks(x) (x)
static int xSemaphoreTake(int lock,int timeout){(void)lock;(void)timeout;assert(!locks);return pdTRUE;}
static void xSemaphoreGive(int lock){(void)lock;assert(!locks);}
static int esp_mqtt_client_publish(int client,const char *topic,const char *payload,int length,int qos,int retain){
    assert(client==1&&!locks&&length==0&&qos==0&&retain==0);
    assert(phase_mqtt_zero_locked(clock_ms));assert(actual_a[0]<1&&actual_a[1]<1&&actual_a[2]<1);
    strcpy(topic_sent,topic);strcpy(payload_sent,payload);published++;
    if(revoked_in_publish){phase_mqtt_allowed=false;phase_mqtt_cancel_locked(true);}
    return result;
}
static void vTaskDelay(int delay){assert(delay==100&&!locks);}
static bool direct_read_ok=true,direct_on;static int direct_writes;
static bool phase_output_direct_read(uint8_t transport,const char *host,unsigned id,bool *on){
    assert(!locks&&(transport==1||transport==2)&&!strcmp(host,"192.168.1.88")&&id==0);*on=direct_on;return direct_read_ok;
}
static bool phase_output_direct_write(uint8_t transport,const char *host,unsigned id,bool on){
    assert(!locks&&(transport==1||transport==2)&&!strcmp(host,"192.168.1.88")&&id==0&&phase_mqtt_zero_locked(clock_ms));
    direct_writes++;published++;strcpy(payload_sent,on?"true":"false");direct_on=on;return result>=0;
}
static bool phase_output_direct_read_auth(uint8_t transport,const char *host,unsigned id,const char *password,bool *on){
    if(transport==3){assert(!strcmp(password,"synthetic-secret"));transport=2;}
    return phase_output_direct_read(transport,host,id,on);
}
static bool phase_output_direct_write_auth(uint8_t transport,const char *host,unsigned id,const char *password,bool on){
    if(transport==3){assert(!strcmp(password,"synthetic-secret"));transport=2;}
    return phase_output_direct_write(transport,host,id,on);
}
#include "phase_mqtt_network.inc"
static void pump(void){(void)phase_mqtt_task;phase_mqtt_publish_once();}
static void fixture(void){
    settings_defaults(&settings);settings.phase_actuator_type=2;settings.phase_mqtt_output_enabled=true;
    settings.phase_switch_enabled=settings.relay_board_enabled=settings.mqtt_enabled=true;
    settings.phase_feedback_enabled=false;settings.relay1_mode=3;settings.xemex_coils=3;
    settings.charge_phases=settings.manual_phases=3;settings.mode=MODE_MANUAL;settings.enabled=true;
    phase_mqtt_allowed=true;phase_mqtt_saving=false;phase_state=PHASE_READY;
    phase_goal=phase_candidate=3;phase_candidate_at=phase_stop_at=phase_zero_at=phase_move_at=phase_hold_until=phase_attempt_at=phase_no_load_since=0;
    phase_idle_inhibit=phase_startup=restarting=ota_in_progress=false;phase_gpio_ready=relay_ready=true;
    mqtt_online=wifi_online=true;relay_on[0]=false;published=result=revoked_in_publish=locks=0;
    actual_at=meter_at=wallbox_at=last_sent_current_at=clock_ms=1000;memset(actual_a,0,sizeof(actual_a));
    memset(&evcc_control,0,sizeof(evcc_control));memset(&pv_control,0,sizeof(pv_control));memset(&manual_filter,0,sizeof(manual_filter));
    phase_mqtt_pending=phase_mqtt_inflight=false;phase_mqtt_known=phase_mqtt_seen_connection=true;phase_mqtt_assumed=3;phase_mqtt_sent_at=1000;phase_mqtt_generation=0;
    direct_read_ok=true;direct_on=false;direct_writes=0;phase_output_direct_online=false;phase_output_direct_at=phase_output_health_due=0;
}
static void step(int64_t now,float a1,float a2,float a3){
    actual_at=meter_at=wallbox_at=last_sent_current_at=clock_ms=now;actual_a[0]=a1;actual_a[1]=a2;actual_a[2]=a3;
    if(settings.phase_output_transport>0)pump();phase_step_locked(now);pump();
}
static void one_phase(void){
    settings.manual_phases=1;step(2000,10,0,0);step(3000,10,0,0);assert(phase_state==PHASE_STOPPING&&!published);
    step(4000,0,0,0);step(11000,0,0,0);assert(!published);step(12000,0,0,0);
    assert(published==1&&!strcmp(payload_sent,"true")&&!strcmp(topic_sent,"wallbox-ems/output/single_phase"));
    step(13000,0,0,0);assert(phase_state==PHASE_MOVING);step(14000,0,0,0);
    step(19000,0,0,0);assert(phase_state==PHASE_READY&&settings.charge_phases==1);
}
int main(void){
    fixture();assert(settings_valid(&settings)&&phase_mqtt_supported_locked());
    char topic[128];assert(phase_mqtt_output_topic(&settings,topic,sizeof(topic)));
    for(const char **p=(const char *[]){"wallbox-ems/command/phases","wallbox-ems/input/phases","bad/+","bad/#","$SYS/set",NULL};*p;p++){
        strcpy(settings.phase_mqtt_topic,*p);assert(!phase_mqtt_output_topic(&settings,topic,sizeof(topic)));
    }
    strcpy(settings.phase_mqtt_topic,"custom/phase/set");assert(phase_mqtt_output_topic(&settings,topic,sizeof(topic)));
    settings_t old=settings,migrated;old.version=28;
    assert(settings_decode(&old,(offsetof(settings_t,phase_mqtt_output_enabled)+3)&~(size_t)3,&migrated));
    assert(!migrated.phase_mqtt_output_enabled&&!migrated.phase_mqtt_topic[0]);
    old=settings;old.version=29;old.phase_mqtt_true_three_phase=true;
    assert(settings_decode(&old,(offsetof(settings_t,phase_mqtt_true_three_phase)+3)&~(size_t)3,&migrated));
    assert(migrated.phase_mqtt_output_enabled&&!migrated.phase_mqtt_true_three_phase);
    old=settings;old.version=30;old.phase_output_transport=2;
    assert(settings_decode(&old,(offsetof(settings_t,phase_output_transport)+3)&~(size_t)3,&migrated));
    assert(!migrated.phase_output_transport&&!migrated.phase_output_host[0]);
    assert(!strcmp(migrated.phase_mqtt_topic,"custom/phase/set"));
    /* NO contactor: false for one phase, true for three. Same zero/wait logic. */
    fixture();settings.phase_mqtt_true_three_phase=true;settings.manual_phases=1;
    step(2000,10,0,0);step(3000,10,0,0);step(4000,0,0,0);step(11000,0,0,0);assert(!published);
    step(12000,0,0,0);assert(published==1&&!strcmp(payload_sent,"false"));
    step(14000,0,0,0);step(19000,0,0,0);assert(settings.charge_phases==1&&phase_state==PHASE_READY);
    settings.manual_phases=3;step(20000,8,0,0);step(21000,8,0,0);step(22000,0,0,0);step(30000,0,0,0);
    assert(published==2&&!strcmp(payload_sent,"true"));step(32000,0,0,0);step(37000,0,0,0);
    assert(settings.charge_phases==3&&phase_state==PHASE_READY);
    assert(settings_decode(&settings,sizeof(settings),&migrated)&&migrated.phase_mqtt_true_three_phase);
    fixture();settings.phase_mqtt_true_three_phase=true;settings.enabled=false;phase_mqtt_setup_locked(1000);
    step(2000,4,0,0);step(3000,0,0,0);step(10000,0,0,0);assert(!published);step(11000,0,0,0);
    assert(published==1&&!strcmp(payload_sent,"true"));step(13000,0,0,0);step(18000,0,0,0);assert(phase_state==PHASE_READY);
    fixture();one_phase();settings.manual_phases=3;
    step(20000,8,0,0);step(21000,8,0,0);step(22000,0,0,0);step(30000,0,0,0);
    assert(published==2&&!strcmp(payload_sent,"false"));step(32000,0,0,0);step(37000,0,0,0);
    assert(settings.charge_phases==3&&phase_state==PHASE_READY);step(38000,10,0,0);assert(published==2);
    fixture();settings.manual_phases=1;step(2000,0,0,0);step(3000,0,0,0);
    for(int64_t t=4000;t<=14000;t+=1000)step(t,0,0,4);assert(!published&&!phase_zero_at);
    step(15000,NAN,0,0);assert(!published&&!phase_zero_at);step(16000,-1,0,0);assert(!published&&!phase_zero_at);
    step(17000,0,0,0);step(25000,0,0,0);assert(published==1);
    /* Revocation drops an unissued job and never parks the external relay. */
    fixture();settings.manual_phases=1;step(2000,10,0,0);step(3000,10,0,0);
    actual_at=meter_at=wallbox_at=last_sent_current_at=clock_ms=4000;memset(actual_a,0,sizeof(actual_a));phase_step_locked(clock_ms);
    actual_at=meter_at=wallbox_at=last_sent_current_at=clock_ms=12000;phase_step_locked(clock_ms);assert(phase_mqtt_pending&&!published);
    phase_mqtt_allowed=false;phase_mqtt_cancel_locked(true);pump();step(13000,0,0,0);
    assert(!published&&!settings.enabled&&phase_state==PHASE_FAULT);
    fixture();one_phase();phase_mqtt_allowed=false;phase_mqtt_cancel_locked(true);step(20000,0,0,0);assert(published==1);
    /* A late completion cannot restore state after revocation. */
    fixture();revoked_in_publish=1;settings.manual_phases=1;
    step(2000,0,0,0);step(3000,0,0,0);step(4000,0,0,0);step(12000,0,0,0);assert(published==1&&!phase_mqtt_known&&phase_state==PHASE_FAULT);
    pump();assert(published==1);
    fixture();result=-1;settings.manual_phases=1;step(2000,0,0,0);step(3000,0,0,0);step(4000,0,0,0);step(12000,0,0,0);
    assert(published==1&&phase_state==PHASE_FAULT);result=0;step(12000,0,0,0);assert(published==1);
    fixture();one_phase();mqtt_online=false;step(20000,8,0,0);assert(phase_state==PHASE_FAULT&&!settings.enabled&&!phase_mqtt_known);
    mqtt_online=true;step(21000,0,0,0);assert(published==1&&phase_state==PHASE_FAULT);
    /* Boot never emits until fresh post-stop zero data confirms eight seconds. */
    fixture();settings.enabled=false;phase_mqtt_setup_locked(1000);assert(!published&&phase_state==PHASE_STOPPING);
    step(2000,4,0,0);step(3000,0,0,0);step(10000,0,0,0);assert(!published);step(11000,0,0,0);
    assert(published==1&&!strcmp(payload_sent,"false"));step(13000,0,0,0);step(18000,0,0,0);assert(phase_state==PHASE_READY);
    fixture();settings.mode=MODE_PV;settings.zero_feed_enabled=true;solar_w=3300;
    step(2000,0,0,0);step(3000,0,0,0);assert(phase_goal==1&&phase_state==PHASE_STOPPING);
    step(4000,0,0,0);step(12000,0,0,0);assert(!strcmp(payload_sent,"true"));
    fixture();evcc_control.configured=evcc_control.enabled=true;evcc_control.phases=1;
    step(2000,0,0,0);step(3000,0,0,0);step(4000,0,0,0);step(12000,0,0,0);assert(!strcmp(payload_sent,"true"));
    fixture();assert(!phase_mqtt_stopped_locked());settings.enabled=false;assert(phase_mqtt_stopped_locked());
    for(unsigned external=0;external<2;external++){
        fixture();settings.phase_mqtt_true_three_phase=true;
        if(external){evcc_control.configured=evcc_control.enabled=true;evcc_control.phases=1;}
        else{settings.mode=MODE_PV;settings.zero_feed_enabled=true;solar_w=3300;}
        step(2000,0,0,0);step(3000,0,0,0);step(4000,0,0,0);step(12000,0,0,0);
        assert(!strcmp(payload_sent,"false"));
    }
    for(unsigned coils=2;coils<=3;coils++)for(unsigned transport=1;transport<=3;transport++)for(unsigned polarity=0;polarity<=1;polarity++){
        fixture();settings.xemex_coils=coils;settings.phase_output_transport=transport;if(transport==3)strcpy(settings.phase_output_password,"synthetic-secret");strcpy(settings.phase_output_host,"192.168.1.88");
        settings.phase_mqtt_true_three_phase=polarity;settings.mqtt_enabled=mqtt_online=false;settings.vehicle_soc_enabled=settings.homeassistant_enabled=settings.pv_display_enabled=false;direct_on=polarity;
        assert(settings_valid(&settings));phase_output_direct_online=true;phase_output_direct_at=1000;
        settings.manual_phases=1;step(2000,10,0,0);step(3000,10,0,0);step(4000,0,0,0);step(11000,0,0,0);assert(!direct_writes);
        step(12000,0,0,0);assert(direct_writes==1&&!strcmp(payload_sent,polarity?"false":"true"));
        step(14000,0,0,0);step(19000,0,0,0);assert(phase_state==PHASE_READY&&settings.charge_phases==1);
        direct_on=!direct_on;step(21000,8,0,0);assert(phase_state==PHASE_FAULT&&!settings.enabled&&direct_writes==1);
        direct_on=!direct_on;step(23000,0,0,0);assert(phase_state==PHASE_FAULT&&direct_writes==1);
        phase_mqtt_allowed=false;phase_mqtt_cancel_locked(true);step(25000,0,0,0);assert(direct_writes==1);
        direct_read_ok=false;step(27000,0,0,0);assert(direct_writes==1&&!phase_mqtt_known);
    }
    /* Initial HTTP failure permits a bounded connection wait, not output.
       Once online, ordinary zero hold and later disconnect protection apply. */
    fixture();settings.enabled=false;settings.phase_output_transport=2;strcpy(settings.phase_output_host,"192.168.1.88");
    phase_mqtt_setup_locked(1000);direct_read_ok=false;step(2000,0,0,0);step(12000,0,0,0);
    assert(phase_state==PHASE_STOPPING&&!published&&!phase_mqtt_known);
    direct_read_ok=true;step(14000,0,0,0);step(21000,0,0,0);assert(!published);
    step(22000,0,0,0);assert(published==1);step(24000,0,0,0);step(29000,0,0,0);
    assert(phase_state==PHASE_READY);direct_read_ok=false;step(31000,0,0,0);assert(phase_state==PHASE_FAULT);
    fixture();settings.enabled=false;settings.phase_output_transport=2;strcpy(settings.phase_output_host,"192.168.1.88");
    phase_mqtt_setup_locked(1000);direct_read_ok=false;step(62000,0,0,0);
    assert(phase_state==PHASE_FAULT&&!published);
    puts("PASS: actual expert output worker: MQTT/Modbus/RPC, startup deadlines, both polarities, zero gating, no replay and revocation");
}
