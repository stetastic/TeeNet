#include "opendtu_mqtt.h"
#include <assert.h>
#include <stdio.h>
#include <string.h>
int main(void){
    settings_t cfg;settings_defaults(&cfg);cfg.basic_mode=false;cfg.pv_display_enabled=true;cfg.mqtt_input_source=2;cfg.battery_protect=true;cfg.zero_feed_enabled=true;
    strcpy(cfg.opendtu_pv_topic,"opendtu/victron/123/PPV");assert(settings_valid(&cfg));
    opendtu_state_t s={0};float charge,discharge;
    assert(opendtu_receive(&s,&cfg,"opendtu/battery/stateOfCharge","72",false,1000));
    assert(!opendtu_battery_fresh(&s,1000));
    assert(opendtu_receive(&s,&cfg,"opendtu/battery/dataAge","0",false,1000));
    assert(opendtu_receive(&s,&cfg,"opendtu/battery/voltage","50",false,1000));
    assert(opendtu_receive(&s,&cfg,"opendtu/battery/current","10",false,1000));
    assert(opendtu_power(&s,&cfg,1000,&charge,&discharge)&&charge==500&&discharge==0);
    assert(opendtu_receive(&s,&cfg,"opendtu/battery/current","-80",false,2000));
    assert(opendtu_power(&s,&cfg,2000,&charge,&discharge)&&charge==0&&discharge==4000);
    cfg.opendtu_current_positive_discharge=true;
    assert(opendtu_power(&s,&cfg,2000,&charge,&discharge)&&charge==4000&&discharge==0);
    assert(opendtu_receive(&s,&cfg,"opendtu/battery/stateOfCharge","99",true,2500)&&s.soc==72);
    assert(!opendtu_battery_fresh(&s,31000));
    assert(opendtu_receive(&s,&cfg,"opendtu/battery/dataAge","29",false,5000));
    assert(opendtu_battery_fresh(&s,5000)&&!opendtu_battery_fresh(&s,6000));
    assert(!opendtu_receive(&s,&cfg,"opendtu/battery/current","NaN",false,5000));
    assert(!opendtu_power(&s,&cfg,5000,&charge,&discharge));
    assert(opendtu_receive(&s,&cfg,"opendtu/victron/123/PPV","1350",false,7000));
    assert(opendtu_pv_fresh(&s,&cfg,7000)&&s.pv==1350);
    assert(!opendtu_receive(&s,&cfg,"opendtu/ac/power","4000",false,7000));
    strcpy(cfg.opendtu_pv_valid_topic,"opendtu/ac/is_valid");assert(!opendtu_pv_fresh(&s,&cfg,7000));
    assert(opendtu_receive(&s,&cfg,"opendtu/ac/is_valid","1",true,7000));assert(!opendtu_pv_fresh(&s,&cfg,7000));
    assert(opendtu_receive(&s,&cfg,"opendtu/ac/is_valid","1",false,7000));assert(opendtu_pv_fresh(&s,&cfg,7000));
    assert(opendtu_receive(&s,&cfg,"opendtu/dtu/status","offline",true,7100));assert(!opendtu_pv_fresh(&s,&cfg,7100));
    assert(opendtu_receive(&s,&cfg,"opendtu/dtu/status","online",false,7200));assert(!opendtu_pv_fresh(&s,&cfg,7200));
    cfg.mqtt_input_source=0;assert(!opendtu_receive(&s,&cfg,"opendtu/battery/stateOfCharge","50",false,7300));
    settings_defaults(&cfg);strcpy(cfg.wallbox_meter_type,"em24_tcp");strcpy(cfg.wallbox_meter_host,"em24.local");cfg.wallbox_tx_pin=6;cfg.wallbox_rx_pin=5;
    assert(settings_valid(&cfg));settings_t reboot;
    assert(settings_decode(&cfg,sizeof(cfg),&reboot));assert(reboot.wallbox_tx_pin==6&&reboot.wallbox_rx_pin==5);
    cfg.basic_mode=true;assert(settings_decode(&cfg,sizeof(cfg),&reboot));assert(reboot.wallbox_tx_pin==6&&reboot.wallbox_rx_pin==5);
    cfg.version=25;assert(settings_decode(&cfg,offsetof(settings_t,opendtu_prefix),&reboot));assert(reboot.version==EMS_SETTINGS_VERSION&&reboot.wallbox_tx_pin==6&&reboot.wallbox_rx_pin==5);
    puts("PASS: OpenDTU retained/stale/offline/invalid/sign handling; custom pins survive full and legacy reboot decoding");
}
