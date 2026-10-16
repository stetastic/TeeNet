#include "ems_core.h"
#include <assert.h>
#include <stdio.h>
#include <string.h>

int main(void) {
    settings_t s;settings_defaults(&s);
    assert(s.basic_mode && settings_valid(&s));
    assert(!s.expert_mode && !s.evcc_feature_enabled && !s.pv_allocation_enabled);
    assert(!s.zero_feed_enabled && !s.battery_protect && !s.vehicle_soc_enabled && !s.pv_display_enabled);
    assert(!s.charge_plan_enabled && !s.external_mode_input_enabled && !s.evu_input_enabled);
    assert(!s.grid_guard_enabled && !s.phase_switch_enabled && !s.relay_board_enabled);
    assert(!s.huawei_enabled && !s.huawei_battery && !s.huawei_pv);
    /* First setup may expose extras, but must never opt into either display. */
    settings_t fresh_setup=s,fresh_reboot;
    fresh_setup.basic_mode=false;
    strcpy(fresh_setup.wifi_ssid,"First setup network");
    assert(settings_decode(&fresh_setup,sizeof(fresh_setup),&fresh_reboot));
    assert(!fresh_reboot.basic_mode && !fresh_reboot.vehicle_soc_enabled && !fresh_reboot.pv_display_enabled);
    /* The public 1.19 installer (schema 27) enabled both displays by default.
       OTA cannot distinguish those saved choices from intentional opt-ins. */
    settings_t installer_119=s;installer_119.version=27;installer_119.basic_mode=false;
    installer_119.vehicle_soc_enabled=installer_119.pv_display_enabled=true;
    size_t installer_length=(offsetof(settings_t,phase_actuator_type)+3)&~(size_t)3;
    assert(settings_decode(&installer_119,installer_length,&fresh_reboot));
    assert(fresh_reboot.vehicle_soc_enabled && fresh_reboot.pv_display_enabled);
    s.basic_mode=true;s.mode=MODE_PV;s.enabled=true;
    s.expert_mode=s.zero_feed_enabled=s.battery_protect=s.vehicle_soc_enabled=true;
    s.pv_display_enabled=s.charge_plan_enabled=s.external_mode_input_enabled=true;
    s.evu_input_enabled=s.grid_guard_enabled=s.phase_switch_enabled=s.relay_board_enabled=true;
    s.huawei_enabled=s.huawei_battery=s.huawei_pv=true;
    strcpy(s.house_meter_type,"huawei");
    s.homeassistant_enabled=true;
    s.mqtt_input_source=2;
    settings_basic_mode(&s);
    assert(!s.enabled && s.mode==MODE_OFF);
    assert(!s.expert_mode && !s.zero_feed_enabled && !s.battery_protect);
    assert(!s.vehicle_soc_enabled && !s.pv_display_enabled && !s.charge_plan_enabled);
    assert(!s.external_mode_input_enabled && !s.evu_input_enabled && !s.grid_guard_enabled);
    assert(!s.phase_switch_enabled && !s.relay_board_enabled && !s.huawei_enabled);
    assert(s.mqtt_input_source==0);
    assert(s.mqtt_enabled && s.homeassistant_enabled && s.basic_mode && settings_valid(&s));
    s.mode=MODE_MANUAL;s.enabled=true;settings_basic_mode(&s);
    assert(s.enabled && s.mode==MODE_MANUAL && s.manual_current_a==8.7f);
    settings_t old;settings_defaults(&old);old.version=24;
    old.phase_switch_enabled=old.relay_board_enabled=true;old.relay1_mode=3;
    old.zero_feed_enabled=old.battery_protect=true;
    /* Padding in an old blob must not become the new profile flag. */
    size_t prefix=offsetof(settings_t,basic_mode),length=(prefix+3)&~(size_t)3;
    memset((char *)&old+prefix,0xff,length-prefix);
    settings_t loaded;assert(settings_decode(&old,length,&loaded));
    assert(!loaded.basic_mode && loaded.phase_switch_enabled && loaded.battery_protect);
    settings_t existing;settings_defaults(&existing);existing.basic_mode=false;
    existing.zero_feed_enabled=existing.battery_protect=existing.vehicle_soc_enabled=existing.pv_display_enabled=true;
    existing.phase_switch_enabled=existing.relay_board_enabled=true;existing.relay1_mode=3;
    existing.wallbox_tx_pin=8;existing.wallbox_rx_pin=9;
    strcpy(existing.wifi_ssid,"Existing network");strcpy(existing.wifi_password,"wifi-test-secret");
    strcpy(existing.mqtt_password,"mqtt-test-secret");
    assert(settings_decode(&existing,sizeof(existing),&loaded));
    assert(!loaded.basic_mode && loaded.zero_feed_enabled && loaded.battery_protect);
    assert(loaded.vehicle_soc_enabled && loaded.pv_display_enabled && loaded.phase_switch_enabled);
    assert(loaded.wallbox_tx_pin==8 && loaded.wallbox_rx_pin==9);
    assert(!strcmp(loaded.wifi_password,existing.wifi_password) && !strcmp(loaded.mqtt_password,existing.mqtt_password));
    settings_t before;settings_defaults(&before);before.vehicle_soc_enabled=before.pv_display_enabled=false;
    settings_t runtime=before,next=before;runtime.mode=MODE_MANUAL;runtime.enabled=true;
    next.basic_mode=true;settings_basic_mode(&next);
    assert(settings_apply_live(&runtime,&before,&next));
    assert(runtime.basic_mode && runtime.enabled && runtime.mode==MODE_MANUAL);
    settings_t disconnected;settings_defaults(&disconnected);disconnected.mqtt_enabled=false;disconnected.mqtt_input_source=2;
    settings_basic_mode(&disconnected);assert(disconnected.mqtt_input_source==0);
    puts("PASS: basic profile disables extras, retains MQTT/manual control, preserves old configurations and ignores old padding");
    return 0;
}
