#include "ems_core.h"
#include "opendtu_mqtt.h"
#include "cJSON.h"
#include <assert.h>
#include <math.h>
#include <stdio.h>
#include <string.h>

typedef int esp_mqtt_client_handle_t;
typedef void *esp_event_base_t;
typedef struct {
    int client,topic_len,total_data_len,current_data_offset,data_len;
    const char *topic,*data;bool retain;
} mqtt_test_event_t;
typedef mqtt_test_event_t *esp_mqtt_event_handle_t;
enum {MQTT_EVENT_CONNECTED,MQTT_EVENT_DISCONNECTED,MQTT_EVENT_DATA};
static settings_t settings;
static bool wifi_online=true;
static opendtu_state_t opendtu;
static bool mqtt_online,phase_mqtt_seen_connection,phase_mqtt_allowed,phase_mqtt_pending;
static unsigned phase_goal,mqtt_rejected,apply_calls,cancel_calls,subscriptions;
static int locks;
static int64_t battery_soc_at,battery_charge_at,battery_discharge_at,car_soc_at,pv_generation_at,power_at,pv_at;
static float car_soc,battery_soc,battery_charge_w,battery_discharge_w,pv_generation_w,wallbox_w;
static char subscribed[20][192];
#define LOCK() assert(locks++==0)
#define UNLOCK() assert(--locks==0)
static int64_t now_ms(void){return 10000;}
static bool sdm_meter(void){return false;}
static bool network_wallbox_meter(void){return false;}
static void phase_mqtt_cancel_locked(bool stop){assert(locks==1);(void)stop;cancel_calls++;}
static bool apply_control(cJSON *body){(void)body;apply_calls++;return true;}
static void homeassistant_discovery(bool enabled){(void)enabled;}
static int esp_mqtt_client_subscribe(int client,const char *topic,int qos){
    assert(client==1&&(qos==0||qos==1)&&subscriptions<20);
    snprintf(subscribed[subscriptions++],sizeof(subscribed[0]),"%s",topic);return 1;
}
static int esp_mqtt_client_enqueue(int client,const char *topic,const char *payload,int size,int qos,int retain,bool store){
    (void)client;(void)topic;(void)payload;(void)size;(void)qos;(void)retain;(void)store;return 1;
}
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wunused-parameter"
#include "mqtt_input_actual.inc"
#pragma GCC diagnostic pop

static void deliver(const char *topic,const char *value,bool retained,bool fragmented){
    int size=(int)strlen(value),split=fragmented?size/2:size;
    mqtt_test_event_t event={.client=1,.topic=topic,.topic_len=(int)strlen(topic),.total_data_len=size,
        .current_data_offset=0,.data_len=split,.data=value,.retain=retained};
    mqtt_event(NULL,NULL,MQTT_EVENT_DATA,&event);
    if(fragmented){event.current_data_offset=split;event.data=value+split;event.data_len=size-split;
        mqtt_event(NULL,NULL,MQTT_EVENT_DATA,&event);}
}
int main(void){
    assert(!heatpump_fresh_locked(now_ms()));
    unsigned checked=0;
    for(unsigned allowed=0;allowed<2;allowed++)for(unsigned polarity=0;polarity<2;polarity++)
    for(unsigned source=0;source<2;source++)for(unsigned charging=0;charging<2;charging++)for(unsigned transport=0;transport<4;transport++){
        settings_defaults(&settings);settings.mqtt_enabled=true;settings.xemex_coils=3;
        settings.phase_actuator_type=2;settings.phase_switch_enabled=settings.relay_board_enabled=true;
        settings.phase_feedback_enabled=false;settings.relay1_mode=3;
        settings.phase_mqtt_output_enabled=phase_mqtt_allowed=allowed;
        settings.phase_mqtt_true_three_phase=polarity;settings.enabled=charging;
        settings.phase_output_transport=transport;strcpy(settings.phase_output_host,"192.168.1.88");
        settings.mqtt_input_source=source?2:0;settings.battery_protect=settings.pv_display_enabled=true;
        strcpy(settings.phase_mqtt_topic,"external/contactor/set");
        phase_goal=3;phase_mqtt_pending=false;subscriptions=apply_calls=cancel_calls=mqtt_rejected=0;
        memset(&opendtu,0,sizeof(opendtu));mqtt_online=phase_mqtt_seen_connection=false;
        mqtt_test_event_t event={.client=1};mqtt_event(NULL,NULL,MQTT_EVENT_CONNECTED,&event);
        assert(mqtt_online);
        for(unsigned i=0;i<subscriptions;i++){
            assert(strcmp(subscribed[i],"external/contactor/set"));
            assert(strcmp(subscribed[i],"wallbox-ems/output/single_phase"));
            assert(!strstr(subscribed[i],"output/#"));
        }
        const settings_t before=settings;
        const char *topics[]={"wallbox-ems/output/single_phase","external/contactor/set",
            "wallbox-ems/command/phases","wallbox-ems/command/phase","wallbox-ems/input/phases",
            "wallbox-ems/command/phase_mqtt_true_three_phase","wallbox-ems/command/phase_mqtt_output_enabled"};
        const char *values[]={"true","false","1","3","auto","{\"true_three_phase\":true}","{\"enabled\":true}"};
        for(unsigned t=0;t<sizeof(topics)/sizeof(topics[0]);t++)
        for(unsigned v=0;v<sizeof(values)/sizeof(values[0]);v++)
        for(unsigned retained=0;retained<2;retained++)for(unsigned fragmented=0;fragmented<2;fragmented++){
            deliver(topics[t],values[v],retained,fragmented);checked++;
            assert(!memcmp(&settings,&before,sizeof(settings)));
            assert(phase_mqtt_allowed==(bool)allowed&&!phase_mqtt_pending&&phase_goal==3);
            assert(apply_calls==0&&cancel_calls==0&&!locks);
        }
        /* A valid normal command proves the parser is running, not a stub. */
        deliver("wallbox-ems/command/enabled","false",false,false);assert(apply_calls==1);
        deliver("wallbox-ems/command/enabled","true",true,false);assert(apply_calls==1);
        if(transport){mqtt_event(NULL,NULL,MQTT_EVENT_DISCONNECTED,&event);assert(cancel_calls==0&&phase_mqtt_allowed==(bool)allowed);}
    }
    settings_defaults(&settings);settings.basic_mode=false;settings.heatpump_enabled=true;
    strcpy(settings.heatpump_source,"mqtt");strcpy(settings.heatpump_input_topic,"external/hp/watts");
    subscriptions=0;esp_mqtt_client_subscribe(1,settings.heatpump_input_topic,1);mqtt_online=true;
    deliver("external/hp/watts","2450",false,true);assert(heatpump_power_w==2450&&heatpump_at==now_ms());
    deliver("external/hp/watts","5000",true,false);assert(heatpump_power_w==2450);
    deliver("external/hp/watts","0",false,false);assert(heatpump_power_w==0&&heatpump_fresh_locked(now_ms()));
    deliver("external/hp/watts","unavailable",false,false);assert(!heatpump_at);
    deliver("external/hp/watts","2200",false,false);assert(heatpump_at);
    settings.heatpump_enabled=false;deliver("external/hp/watts","5000",false,false);assert(heatpump_power_w==2200);
    assert(!heatpump_fresh_locked(now_ms()));settings.heatpump_enabled=true;
    mqtt_test_event_t disconnect={.client=1};mqtt_event(NULL,NULL,MQTT_EVENT_DISCONNECTED,&disconnect);assert(!heatpump_at);
    printf("PASS: %u phase MQTT isolation cases plus real heat-pump custom topic, fragments, zero, unavailable, retained/disabled and disconnect handling\n",checked);
}
