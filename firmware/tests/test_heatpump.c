#include "ems_core.h"
#include "shelly_modbus.h"
#include <assert.h>
#include <math.h>
#include <stdio.h>
#include <string.h>

static settings_t settings;
static bool wifi_online=true,mqtt_online=true,network_ok=true;
static float network_w=1800;
static int calls;
static int64_t clock_ms=1000;
static int64_t now_ms(void){return clock_ms;}
#define LOCK() ((void)0)
#define UNLOCK() ((void)0)
static bool read_shelly_modbus(const char *host,const char *type,shelly_modbus_reading_t *out,int timeout){
    assert(!strcmp(host,"192.168.1.40"));
    assert(!strcmp(type,"shelly_gen2")||!strcmp(type,"shelly_em1"));
    assert(timeout==1500);calls++;out->active_power_w=network_w;return network_ok;
}
#define TEE_HEATPUMP_TEST
#include "../main/heatpump_runtime.inc"

int main(void){
    settings_defaults(&settings);assert(!settings.heatpump_enabled);
    assert(heatpump_settings_valid(&settings));
    strcpy(settings.heatpump_input_topic,"invalid/+");strcpy(settings.heatpump_output_topic,"invalid/#");
    assert(heatpump_settings_valid(&settings)); /* Disabled extras must not trap unrelated saves. */
    settings_defaults(&settings);
    settings_t old=settings,migrated;old.version=33;old.price_kwh=.31f;
    size_t old_size=(offsetof(settings_t,heatpump_enabled)+3)&~(size_t)3;
    assert(settings_decode(&old,old_size,&migrated));
    assert(migrated.version==34&&!migrated.heatpump_enabled);
    assert(fabsf(migrated.price_kwh-.31f)<.00001f);
    assert(!strcmp(migrated.heatpump_source,"shelly_gen2"));

    settings.basic_mode=false;settings.heatpump_enabled=true;
    assert(!heatpump_settings_valid(&settings));
    strcpy(settings.heatpump_host,"192.168.1.40");assert(heatpump_settings_valid(&settings));
    assert(heatpump_poll_once()&&calls==1&&heatpump_power_w==1800);
    assert(heatpump_fresh_locked(clock_ms));
    clock_ms+=EMS_INPUT_TTL+1;assert(!heatpump_fresh_locked(clock_ms));
    network_w=0;assert(heatpump_poll_once()&&heatpump_fresh_locked(clock_ms)&&heatpump_power_w==0);
    network_w=-1;assert(!heatpump_poll_once()&&!heatpump_fresh_locked(clock_ms));
    network_w=NAN;assert(!heatpump_poll_once());
    network_ok=false;assert(!heatpump_poll_once()&&heatpump_at==0);
    network_ok=true;network_w=2300;
    strcpy(settings.heatpump_source,"shelly_em1");assert(heatpump_poll_once()&&heatpump_power_w==2300);
    wifi_online=false;int previous=calls;assert(!heatpump_poll_once()&&calls==previous&&!heatpump_fresh_locked(clock_ms));wifi_online=true;

    strcpy(settings.heatpump_source,"mqtt");assert(!heatpump_settings_valid(&settings));
    strcpy(settings.mqtt_uri,"mqtt://broker.local");assert(heatpump_settings_valid(&settings));
    previous=calls;assert(!heatpump_poll_once()&&calls==previous);
    assert(!heatpump_mqtt_locked(settings.heatpump_input_topic,"2500",true));assert(heatpump_at==0);
    assert(heatpump_mqtt_locked(settings.heatpump_input_topic,"2500",false));assert(heatpump_fresh_locked(clock_ms)&&heatpump_power_w==2500);
    assert(heatpump_mqtt_locked(settings.heatpump_input_topic,"0",false));assert(heatpump_fresh_locked(clock_ms)&&heatpump_power_w==0);
    assert(!heatpump_mqtt_locked(settings.heatpump_input_topic,"-10",false)&&heatpump_at==0);
    assert(!heatpump_mqtt_locked(settings.heatpump_input_topic,"NaN",false));
    assert(heatpump_mqtt_locked(settings.heatpump_input_topic,"unavailable",false)&&heatpump_at==0);
    assert(!heatpump_mqtt_locked("wrong/topic","500",false));
    assert(heatpump_mqtt_locked(settings.heatpump_input_topic,"800",false));
    mqtt_online=false;assert(!heatpump_fresh_locked(clock_ms));mqtt_online=true;
    strcpy(settings.heatpump_output_topic,settings.heatpump_input_topic);assert(!heatpump_settings_valid(&settings));
    strcpy(settings.heatpump_output_topic,"wallbox-ems/command/power_kw");assert(!heatpump_settings_valid(&settings));
    strcpy(settings.heatpump_output_topic,"wallbox-ems/input/pv_generation_w");assert(!heatpump_settings_valid(&settings));
    strcpy(settings.heatpump_output_topic,"hp/power_w");assert(heatpump_settings_valid(&settings));
    strcpy(settings.heatpump_input_topic,"hp/+");assert(!heatpump_settings_valid(&settings));
    strcpy(settings.heatpump_input_topic,"hp/input");settings.heatpump_enabled=false;assert(!heatpump_mqtt_locked("hp/input","800",false));
    assert(!heatpump_fresh_locked(clock_ms));
    settings.heatpump_enabled=true;settings.basic_mode=true;settings_basic_mode(&settings);assert(!settings.heatpump_enabled);
    puts("Heat pump: v33 migration, both Shelly profiles, zero/invalid/stale/offline readings, MQTT isolation and disabled/basic mode passed");
    return 0;
}
