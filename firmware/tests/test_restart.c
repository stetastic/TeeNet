#include "ems_core.h"
#include <assert.h>
#include <stdio.h>
#include <string.h>

typedef struct { const char *name; size_t offset; char type; bool restart; } change_t;
#define FIELD(name,type,restart) {#name,offsetof(settings_t,name),type,restart}
static const change_t changes[]={
    FIELD(heatpump_enabled,'b',true),FIELD(heatpump_publish_enabled,'b',true),
    FIELD(heatpump_source,'s',true),FIELD(heatpump_host,'s',true),FIELD(heatpump_input_topic,'s',true),FIELD(heatpump_output_topic,'s',true),
    FIELD(wifi_ssid,'s',true),FIELD(wifi_password,'s',true),
    FIELD(mqtt_enabled,'b',true),FIELD(mqtt_uri,'s',true),FIELD(mqtt_username,'s',true),
    FIELD(mqtt_password,'s',true),FIELD(mqtt_prefix,'s',true),
    FIELD(shell_rs485_interface,'u',true),FIELD(shell_rs485_host,'s',true),
    FIELD(wallbox_tx_pin,'i',true),FIELD(wallbox_rx_pin,'i',true),
    FIELD(wallbox_meter_type,'s',true),FIELD(meter_rs485_interface,'u',true),FIELD(meter_rs485_host,'s',true),
    FIELD(xemex_address,'u',true),FIELD(meter_baud,'i',true),FIELD(meter_format,'u',true),FIELD(xemex_coils,'u',true),
    FIELD(house_meter_type,'s',true),FIELD(house_meter_host,'s',false),FIELD(house_power_path,'s',false),
    FIELD(house_rs485_interface,'u',true),FIELD(house_rs485_host,'s',true),
    FIELD(house_address,'u',true),FIELD(house_meter_baud,'i',true),FIELD(house_meter_format,'u',true),
    FIELD(fixed_charge_phases,'u',true),FIELD(relay_board_enabled,'b',true),FIELD(relay_active_low,'b',true),
    FIELD(relay1_mode,'u',true),FIELD(phase_switch_enabled,'b',true),FIELD(phase_feedback_enabled,'b',true),
    FIELD(phase_feedback_closed_is_single,'b',true),FIELD(external_mode_input_enabled,'b',true),FIELD(evu_input_enabled,'b',true),
    FIELD(price_kwh,'f',false),FIELD(solar_price_kwh,'f',false),FIELD(basic_mode,'b',false),
    FIELD(zero_feed_enabled,'b',false),FIELD(zero_reserve_w,'f',false),FIELD(grid_guard_enabled,'b',false),
    FIELD(grid_limit_a,'f',false),FIELD(max_charge_a,'f',false),FIELD(evu_limit_a,'f',false),
    FIELD(shell_limits_auto,'b',false),FIELD(shell_setup_host,'s',false),
    FIELD(huawei_enabled,'b',false),FIELD(huawei_host,'s',false),FIELD(huawei_unit_id,'u',false),
    FIELD(huawei_battery,'b',false),FIELD(huawei_pv,'b',false),
    FIELD(battery_protect,'b',false),FIELD(battery_reserve_soc,'f',false),
    FIELD(battery_cloud_limit_w,'f',false),FIELD(battery_assist_limit_w,'f',false),
    FIELD(pv_allocation_enabled,'b',false),FIELD(pv_priority,'u',false),
    FIELD(pv_house_priority_w,'f',false),FIELD(pv_car_priority_w,'f',false),
    FIELD(vehicle_soc_enabled,'b',false),FIELD(pv_display_enabled,'b',false),FIELD(charge_plan_enabled,'b',false),
    FIELD(evcc_feature_enabled,'b',false),FIELD(opendtu_current_positive_discharge,'b',false),
    FIELD(wallbox_meter_host,'s',false)
};
int main(void){
    for(size_t i=0;i<sizeof(changes)/sizeof(changes[0]);i++){
        settings_t stored,runtime,next;settings_defaults(&stored);next=stored;runtime=stored;
        runtime.enabled=true;runtime.mode=MODE_MANUAL;runtime.manual_current_a=12;
        const change_t *c=&changes[i];void *p=(char *)&next+c->offset;
        if(c->type=='s')strcpy(p,"changed");
        else if(c->type=='b')*(bool *)p=!*(bool *)p;
        else if(c->type=='u')(*(uint8_t *)p)++;
        else if(c->type=='i')(*(int *)p)++;
        else *(float *)p+=.1f;
        settings_t old=runtime;
        bool applied=settings_apply_live(&runtime,&stored,&next);
        if(applied==c->restart){fprintf(stderr,"Wrong restart classification: %s\n",c->name);assert(0);}
        if(!applied)assert(!memcmp(&runtime,&old,sizeof(runtime)));
        else assert(runtime.enabled && runtime.mode==MODE_MANUAL);
    }
    settings_t stored,runtime,next;settings_defaults(&stored);runtime=stored;next=stored;
    runtime.enabled=true;runtime.mode=MODE_MANUAL;runtime.max_charge_a=12;runtime.grid_limit_a=35;
    runtime.manual_current_a=12;next.price_kwh=.3f;
    assert(settings_apply_live(&runtime,&stored,&next));
    assert(runtime.max_charge_a==12 && runtime.grid_limit_a==35 && runtime.manual_current_a==12);
    next=stored;next.max_charge_a=10;
    assert(settings_apply_live(&runtime,&stored,&next));assert(runtime.manual_current_a==10);
    stored.mqtt_input_source=2;runtime=stored;next=stored;
    next.opendtu_current_positive_discharge=true;
    assert(settings_apply_live(&runtime,&stored,&next));assert(runtime.opendtu_current_positive_discharge);
    next=stored;strcpy(next.opendtu_pv_topic,"new/pv");assert(!settings_apply_live(&runtime,&stored,&next));
    next=stored;next.battery_protect=true;assert(!settings_apply_live(&runtime,&stored,&next));
    puts("PASS: every settings group classified; live limits preserve charging and automatic values, boot resources remain restart-only");
    return 0;
}
