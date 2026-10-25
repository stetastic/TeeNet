#include "ems_core.h"
#include "hardware_profile.h"
#include <assert.h>
#include <stdio.h>
#include <string.h>
int main(void){
 settings_t s,out,active;settings_defaults(&s);assert(settings_valid(&s)&&!settings_gpio_modified(&s));
 for(unsigned role=0;role<EMS_GPIO_ROLES;role++)assert(settings_gpio_value(&s,role)==gpio_roles[role].standard);
#if defined(CONFIG_TEE_BOARD_ATOMS3_LITE) && CONFIG_TEE_BOARD_ATOMS3_LITE
 assert(!settings_gpio_reason(38,false)[0]);assert(settings_gpio_reason(17,false)[0]);assert(s.status_led_pin==35);
 s.wallbox_tx_pin=7;s.wallbox_rx_pin=8;s.relay1_pin=5;s.phase_feedback_pin=6;
 assert(settings_valid(&s));
#else
 const int changed[]={1,2,8,9,40,41,10,11,15,16,21,47};
 s.basic_mode=false;s.external_mode_input_enabled=s.evu_input_enabled=true;
 s.relay_board_enabled=s.phase_switch_enabled=true;s.relay1_mode=3;s.phase_feedback_count=2;
 strcpy(s.house_meter_type,"xemex");
 for(unsigned role=0;role<EMS_GPIO_ROLES;role++)settings_gpio_set(&s,role,changed[role]);
 assert(settings_valid(&s));
#endif
 assert(settings_gpio_modified(&s));settings_fixed_pins(&s);assert(settings_gpio_modified(&s));
 assert(settings_decode(&s,sizeof(s),&out)&&settings_gpio_equal(&s,&out));
 active=out;out.evu_input_pin=2;
 assert(!settings_apply_live(&active,&s,&out)&&settings_gpio_equal(&active,&s));
 settings_defaults(&s);s.status_led_pin=-1;assert(settings_valid(&s));
 s.xemex_tx_pin=s.wallbox_tx_pin;assert(!settings_valid(&s));
 s.meter_rs485_interface=1;strcpy(s.meter_rs485_host,"192.168.1.20:8899");assert(settings_valid(&s));
 s.xemex_tx_pin=34;assert(!settings_valid(&s));
 settings_defaults(&s);s.version=32;s.xemex_tx_pin=47;
 size_t old_size=offsetof(settings_t,phase_feedback_pin);assert(settings_decode(&s,old_size,&out));
 assert(out.version==EMS_SETTINGS_VERSION&&out.xemex_tx_pin==EMS_DEFAULT_METER_TX_PIN&&out.phase_feedback_pin==EMS_DEFAULT_FEEDBACK_PIN);
 for(int pin=0;pin<=48;pin++)if(pin==0||pin==3||pin==19||pin==20||pin==45||pin==46)assert(settings_gpio_reason(pin,false)[0]);
 puts("PASS: board GPIO availability, 12-role defaults, remapping persistence, active conflicts, dormant sharing, protected pins, v32 migration and restart-only application");
 return 0;
}
