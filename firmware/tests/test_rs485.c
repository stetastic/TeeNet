#include "ems_core.h"
#include <assert.h>
#include <string.h>
#include <stdio.h>
int main(void){
 settings_t s,o;settings_defaults(&s);assert(settings_valid(&s));
 assert(rs485_endpoint_valid("192.168.1.20:8899",64));assert(rs485_endpoint_valid("192.168.1.20",64));
 assert(!rs485_endpoint_valid("hostname:8899",64));assert(!rs485_endpoint_valid("192.168.1.256:8899",64));
 assert(!rs485_endpoint_valid("192.168.1.20:0",64));assert(!rs485_endpoint_valid("192.168.1.20:65536",64));
 assert(!rs485_endpoint_valid("192.168.1.20:8899/path",64));assert(!rs485_endpoint_valid("224.1.1.1",64));
 s.shell_rs485_interface=1;strcpy(s.shell_rs485_host,"192.168.1.20:8899");
 s.wallbox_tx_pin=4;s.wallbox_rx_pin=5;assert(settings_valid(&s)); /* No phantom UART collisions. */
 s.meter_rs485_interface=1;strcpy(s.meter_rs485_host,"192.168.1.20");assert(!settings_valid(&s));
 strcpy(s.meter_rs485_host,"192.168.1.20:8900");assert(settings_valid(&s));
 s.house_rs485_interface=1;strcpy(s.house_meter_type,"sdm630");strcpy(s.house_rs485_host,"192.168.1.20:8901");assert(settings_valid(&s));
 s.basic_mode=false;s.evcc_feature_enabled=true;assert(settings_decode(&s,sizeof(s),&o));
 assert(o.shell_rs485_interface==1 && o.meter_rs485_interface==1 && o.house_rs485_interface==1 && o.evcc_feature_enabled);
 assert(!strcmp(o.shell_rs485_host,s.shell_rs485_host));
 settings_t live,stored;settings_defaults(&live);stored=live;assert(!settings_apply_live(&live,&stored,&s));assert(live.shell_rs485_interface==0);
 s.basic_mode=true;settings_basic_mode(&s);assert(!s.evcc_feature_enabled); /* No hidden external owner in basic mode. */
 settings_defaults(&s);s.version=26;strcpy(s.wifi_ssid,"kept");
 size_t prefix=offsetof(settings_t,shell_rs485_interface),length=(prefix+3)&~(size_t)3;
 unsigned char bytes[sizeof(s)];memcpy(bytes,&s,sizeof(s));memset(bytes+prefix,0xa5,length-prefix);
 assert(settings_decode(bytes,length,&o));assert(!strcmp(o.wifi_ssid,"kept"));assert(!o.evcc_feature_enabled && o.shell_rs485_interface==0);
 assert(o.meter_rs485_interface==0 && o.house_rs485_interface==0);
 puts("PASS: independent transports, endpoint validation/conflicts, NVS migration and reboot-only application");
}
