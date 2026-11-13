#include "ems_core.h"
#include "hardware_profile.h"
#include <assert.h>
#include <stdio.h>
#include <string.h>

int main(void) {
    settings_t s,out;settings_defaults(&s);
    assert(!strcmp(EMS_HARDWARE_ID,"andreas-testboard"));
    assert(!strcmp(EMS_OTA_ASSET_SUFFIX,"-andreas-testboard"));
    assert(s.wallbox_tx_pin==2 && s.wallbox_rx_pin==1 && s.shell_rs485_interface==2);
    assert(!strcmp(s.wallbox_meter_type,"shelly_gen2"));
    assert(s.xemex_tx_pin==1 && s.xemex_rx_pin==2);
    assert(s.external_mode_input_pin==5 && !s.external_mode_input_enabled);
    assert(settings_valid(&s));
    assert(settings_decode(&s,sizeof(s),&out));
    assert(out.wallbox_tx_pin==2 && out.wallbox_rx_pin==1);
    assert(s.evu_input_pin==6);
    out.external_mode_input_pin=1;out.external_mode_input_enabled=true;
    assert(!settings_valid(&out)); /* GPIO1 belongs to the Tail485 Grove port. */
    out.external_mode_input_enabled=false;out.external_mode_input_pin=5;
    out.wallbox_tx_pin=6;assert(!settings_valid(&out));
    settings_fixed_pins(&out);assert(out.wallbox_tx_pin==2 && settings_valid(&out));
    strcpy(out.wallbox_meter_type,"xemex");assert(!settings_valid(&out));
    settings_defaults(&out);out.meter_rs485_interface=2;assert(!settings_valid(&out));
    /* Updating a commissioned Atomic RS485 base must keep its old pins. */
    settings_defaults(&s);s.shell_rs485_interface=0;s.wallbox_tx_pin=6;s.wallbox_rx_pin=5;
    s.version=34;strcpy(s.wifi_ssid,"Andreas-Netz");strcpy(s.mqtt_password,"preserve-me");
    assert(settings_decode(&s,(offsetof(settings_t,phase_feedback_source)+3)&~(size_t)3,&out));
    assert(out.shell_rs485_interface==0 && out.wallbox_tx_pin==6 && out.wallbox_rx_pin==5);
    assert(!strcmp(out.wifi_ssid,"Andreas-Netz") && !strcmp(out.mqtt_password,"preserve-me"));
    assert(!strcmp(EMS_HARDWARE_NAME,"M5Stack AtomS3 Lite + Tail485"));
    puts("PASS: Atom Tail485 fixed pins, single bus, and Andreas OTA identity/legacy pins retained");
    return 0;
}
