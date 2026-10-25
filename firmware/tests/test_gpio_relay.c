#include "ems_core.h"
#include <assert.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
enum{GPIO_MODE_OUTPUT=1,GPIO_MODE_INPUT=2,GPIO_PULLUP_DISABLE=0,GPIO_PULLUP_ENABLE=1,GPIO_PULLDOWN_DISABLE=0,GPIO_PULLDOWN_ENABLE=1,GPIO_INTR_DISABLE=0,ESP_OK=0,PHASE_FAULT=4,PHASE_MOVING=2};
typedef struct{uint64_t pin_bit_mask;int mode,pull_up_en,pull_down_en,intr_type;}gpio_config_t;
static settings_t settings;
static bool relay_ready,phase_gpio_ready,relay_on[1],hardware_low,fail_output;
static uint8_t phase_goal;
static int phase_state,levels[49],writes,first_level,remote_calls;
static int64_t phase_move_at;
static gpio_config_t output_config;
static bool phase_mqtt_remote(void){return settings.phase_actuator_type==2;}
static bool phase_shelly_remote(void){return settings.phase_actuator_type==1||settings.phase_actuator_type==3;}
static bool phase_mqtt_begin_locked(uint8_t goal,int64_t now){(void)goal;(void)now;remote_calls++;return true;}
static bool phase_shelly_begin_locked(uint8_t goal,int64_t now){(void)goal;(void)now;remote_calls++;return true;}
static void phase_mqtt_setup_locked(int64_t now){(void)now;remote_calls++;}
static int64_t now_ms(void){return 1000;}
static void gpio_set_level(int pin,int level){assert(pin==12);if(!writes)first_level=level;writes++;levels[pin]=level;}
static int gpio_config(const gpio_config_t *c){if(c->mode==GPIO_MODE_OUTPUT){output_config=*c;return fail_output?-1:ESP_OK;}return ESP_OK;}
#define PHASE_FEEDBACK_GPIO settings.phase_feedback_pin
#define PHASE_FEEDBACK_SECOND_GPIO settings.phase_feedback_second_pin
#include "gpio_relay_actual.inc"
static unsigned observed(void){
    bool energized=hardware_low?!levels[12]:levels[12];
    return phase_feedback_contacts(!energized,false,1,false);
}
static void fixture(bool low){
    settings_defaults(&settings);settings.basic_mode=false;
    settings.relay_board_enabled=settings.phase_switch_enabled=true;settings.relay1_mode=3;settings.relay_active_low=low;
    relay_ready=phase_gpio_ready=relay_on[0]=fail_output=false;hardware_low=low;
    writes=remote_calls=first_level=phase_state=phase_goal=0;memset(levels,0,sizeof(levels));memset(&output_config,0,sizeof(output_config));
}
int main(void){
    for(unsigned low=0;low<2;low++){
        fixture(low);phase_setup();
        assert(relay_ready&&phase_gpio_ready&&!relay_on[0]&&phase_goal==3&&phase_state==PHASE_MOVING);
        assert(first_level==(int)low&&levels[12]==(int)low&&observed()==3);
        assert(output_config.pull_up_en==(int)low&&output_config.pull_down_en==!(int)low);
        assert(phase_actuator_begin_locked(1,1000)&&relay_on[0]&&observed()==1);
        assert(levels[12]==!(int)low);
        assert(phase_actuator_begin_locked(3,2000)&&!relay_on[0]&&observed()==3);
        fixture(low);fail_output=true;phase_setup();assert(!phase_gpio_ready&&phase_state==PHASE_FAULT);
        fixture(low);settings.relay_board_enabled=false;phase_setup();assert(!writes);
        for(unsigned remote=1;remote<=3;remote++){
            fixture(low);settings.phase_actuator_type=remote;phase_setup();assert(!writes);
            assert(phase_actuator_begin_locked(1,1000)&&!writes);
        }
    }
    puts("PASS: production GPIO setup/output preserves active High/Low, safe initial level, single/three-phase feedback, driver failure and remote GPIO isolation");
}
