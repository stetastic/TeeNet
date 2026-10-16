#include "ems_core.h"
#include "meter_json.h"
#include "shelly_modbus.h"
#include <assert.h>
#include <math.h>
#include <stdio.h>
#include <string.h>
static void near(float a,float b) { if(fabsf(a-b)>=.001f) fprintf(stderr,"near failed: %.6f != %.6f\n",a,b); assert(fabsf(a-b)<.001f); }
static void mixed(uint8_t *data,float value) {
    uint32_t bits;memcpy(&bits,&value,sizeof(bits));
    data[0]=(bits>>8)&255;data[1]=bits&255;data[2]=bits>>24;data[3]=(bits>>16)&255;
}
int main(void) {
    settings_t s,out; settings_defaults(&s); assert(settings_valid(&s));
    float a[]={16,NAN,999}; assert(reconstruct_currents(a,1)); near(a[0]+a[1]+a[2],48);
    float b[]={10,14,NAN}; assert(reconstruct_currents(b,2)); near(b[2],14); near(b[0]+b[1]+b[2],38);
    assert(!reconstruct_currents(b,0)); assert(!reconstruct_currents(b,4)); b[0]=NAN; assert(!reconstruct_currents(b,1));
    /* Preserve old struct layouts with tail padding, independent of appended fields. */
    strcpy(s.wifi_ssid,"saved-network"); strcpy(s.wifi_password,"saved-pass"); s.price_kwh=.42f;
    for(unsigned v=1;v<=EMS_SETTINGS_VERSION;v++) {
        size_t end=v==1?offsetof(settings_t,nominal_v):v==2?offsetof(settings_t,control_verified)+1:v==3?offsetof(settings_t,zero_reserve_w)+4:v==4?offsetof(settings_t,xemex_coils)+1:v==5?offsetof(settings_t,mqtt_state_json)+1:v==6?offsetof(settings_t,battery_reserve_soc)+4:v==7?offsetof(settings_t,current_offset_a)+4:v==8?offsetof(settings_t,phase_switch_enabled):v==9?offsetof(settings_t,expert_mode):v==10?offsetof(settings_t,mqtt_input_source):v==11?offsetof(settings_t,reserved_http_meter_enabled):v==12?offsetof(settings_t,battery_cloud_limit_w):v==13?offsetof(settings_t,reserved_feedback_source):v==14?offsetof(settings_t,house_meter_password):v==15?offsetof(settings_t,wallbox_meter_host):v<18?offsetof(settings_t,charge_plan_enabled):v==18?offsetof(settings_t,huawei_enabled):v==19?offsetof(settings_t,fixed_charge_phases):v==20?offsetof(settings_t,shell_limits_auto):v==21?offsetof(settings_t,phase_feedback_enabled):v==22?offsetof(settings_t,phase_feedback_closed_is_single):v==23?offsetof(settings_t,control_status_visible):v==24?offsetof(settings_t,basic_mode):v==25?offsetof(settings_t,opendtu_prefix):v==26?offsetof(settings_t,shell_rs485_interface):v==27?offsetof(settings_t,phase_actuator_type):v==28?offsetof(settings_t,phase_mqtt_output_enabled):v==29?offsetof(settings_t,phase_mqtt_true_three_phase):v==30?offsetof(settings_t,phase_output_transport):v==31?offsetof(settings_t,phase_feedback_count):v==32?offsetof(settings_t,phase_feedback_pin):sizeof(s);
        unsigned char raw[sizeof(s)]; memcpy(raw,&s,sizeof(s)); memcpy(raw,&v,4);
        memset(raw+end,0xa5,((end+3)&~3u)-end);
        assert(settings_decode(raw,(end+3)&~3u,&out)); assert(!strcmp(out.wifi_ssid,"saved-network"));
        assert(!strcmp(out.wifi_password,"saved-pass")); assert(!out.enabled); assert(out.xemex_coils==1);
        near(out.price_kwh,v<8?.25f:.42f); near(out.solar_price_kwh,.08f);
        assert(!settings_decode(raw,((end+3)&~3u)-1,&out));
    }
    s.version=4; strcpy(s.house_meter_type,"sdm630"); s.zero_feed_enabled=true;
    assert(settings_decode(&s,(offsetof(settings_t,xemex_coils)+4)&~3u,&out));
    assert(!strcmp(out.wallbox_meter_type,"sdm630") && !strcmp(out.house_meter_type,"tasmota")); assert(!out.zero_feed_enabled);
    settings_defaults(&s); s.meter_format=9; assert(!settings_valid(&s)); settings_defaults(&s); s.meter_baud=12345; assert(!settings_valid(&s));
    settings_defaults(&s); s.wallbox_rts_pin=-2; assert(!settings_valid(&s));
    float grid,max;
    assert(!shell_limits_parse("{\"maxLimit\":{\"current\":32},\"connectors\":[{\"id\":1,\"max\":{\"current\":16}}]} trailing",&grid,&max));
    assert(!shell_limits_parse("{\"maxLimit\":{\"current\":32},\"connectors\":[{\"id\":1,\"max\":{\"current\":16}}]",&grid,&max));
    assert(shell_limits_parse("{\"maxLimit\":{\"current\":32},\"connectors\":[{\"id\":1,\"max\":{\"current\":16},\"chargingRate\":\"0kw 3 phase\"}]}",&grid,&max));near(grid,32);near(max,16);
    assert(!shell_limits_parse("{\"maxLimit\":{\"current\":32},\"connectors\":[{\"id\":2,\"max\":{\"current\":16}}]}",&grid,&max));
    assert(!shell_limits_parse("{\"maxLimit\":{\"current\":\"32\"},\"connectors\":[{\"id\":1,\"max\":{\"current\":16}}]}",&grid,&max));
    assert(!shell_limits_parse("{\"maxLimit\":{\"current\":32},\"connectors\":[{\"id\":1,\"max\":{\"current\":0}}]}",&grid,&max));
    assert(!shell_limits_parse("{\"maxLimit\":{\"current\":32},\"connectors\":[{\"id\":1,\"max\":{\"current\":16}},{\"id\":1,\"max\":{\"current\":32}}]}",&grid,&max));
    /* Independent known IEEE754 values: 1A, 2A, 16.5A, 11000W. */
    uint8_t f[]={1,4,12,0x3f,0x80,0,0,0x40,0,0,0,0x41,0x84,0,0,0,0}; append_crc(f,15);
    assert(decode_sdm(f,sizeof(f),1,a,3,0,999)); near(a[0],1);near(a[1],2);near(a[2],16.5);
    assert(!decode_sdm(f,sizeof(f),2,a,3,0,999)); f[4]^=1; assert(!decode_sdm(f,sizeof(f),1,a,3,0,999));
    uint8_t p[]={1,4,4,0x46,0x2b,0xe0,0,0,0}; append_crc(p,7); float watts;
    assert(decode_sdm(p,sizeof(p),1,&watts,1,0,1000000)); near(watts,11000);
    p[3]|=0x80; append_crc(p,7); assert(!decode_sdm(p,sizeof(p),1,&watts,1,0,1000000));
    /* SDM230 has ONLY one current at 0006 and phase power at 000c.
       8 A / 1840 W on that phase -> 8 A per phase, 24 A sum, 5520 W total. */
    const sdm_profile_t *single=sdm_profile("sdm230"),*three=sdm_profile("sdm630");
    assert(single && single->current_register==6 && single->power_register==12 && single->phases==1);
    assert(three && three->current_register==6 && three->power_register==52 && three->phases==3);
    assert(sdm_profile("sdm120")==single);assert(sdm_profile("sdm630mct")==three);
    assert(!sdm_profile("sdm320"));
    uint8_t one_i[]={1,4,4,0x41,0,0,0,0,0},one_p[]={1,4,4,0x44,0xe6,0,0,0,0};
    append_crc(one_i,7); append_crc(one_p,7);
    assert(decode_sdm_reading(single,1,one_i,9,one_p,9,a,&watts));
    near(a[0],8);near(a[1],8);near(a[2],8);near(a[0]+a[1]+a[2],24);near(watts,5520);
    assert(!decode_sdm_reading(three,1,one_i,9,one_p,9,a,&watts));
    one_p[3]|=0x80;append_crc(one_p,7);assert(!decode_sdm_reading(single,1,one_i,9,one_p,9,a,&watts));
    f[4]^=1; p[3]&=0x7f; append_crc(p,7);
    assert(decode_sdm_reading(three,1,f,sizeof(f),p,sizeof(p),a,&watts));near(a[2],16.5);near(watts,11000);
    settings_defaults(&s);s.version=4;strcpy(s.house_meter_type,"sdm320");
    assert(settings_decode(&s,(offsetof(settings_t,xemex_coils)+4)&~3u,&out));assert(!strcmp(out.wallbox_meter_type,"sdm230"));assert(!out.control_verified);
    settings_defaults(&s); s.mode=MODE_MANUAL; s.manual_current_a=8; s.enabled=true;
    assert(settings_decode(&s,sizeof(s),&out)); assert(out.mode==MODE_MANUAL && out.manual_current_a==8 && !out.enabled);
    settings_defaults(&s);strcpy(s.wallbox_meter_type,"sdm230");s.meter_baud=1200;assert(settings_valid(&s));
    settings_defaults(&s);s.grid_guard_enabled=true;assert(!settings_valid(&s));
    strcpy(s.house_meter_type,"sdm630");assert(settings_valid(&s));
    strcpy(s.house_meter_type,"sdm230");assert(!settings_valid(&s));
    strcpy(s.house_meter_type,"xemex");s.house_xemex_coils=3;assert(settings_valid(&s));
    s.house_xemex_coils=1;assert(!settings_valid(&s));
    settings_defaults(&s);s.external_mode_input_enabled=true;s.external_mode_input_pin=s.wallbox_tx_pin;assert(!settings_valid(&s));
    settings_defaults(&s);strcpy(s.wallbox_meter_type,"ads1115");assert(!settings_valid(&s));s.ads1115_enabled=true;assert(!settings_valid(&s));
    assert(house_power_parse("{\"StatusSNS\":{\"ENERGY\":{\"Power\":-2300}}}","tasmota","",&watts)); near(watts,-2300);
    assert(!house_power_parse("{\"Power\":123,\"StatusSNS\":{\"other\":{\"Power\":99}}}","tasmota","",&watts));
    assert(house_power_parse("{\"StatusSNS\":{\"meter\":{\"power\":-400}}}","tasmota","StatusSNS.meter.power",&watts)); near(watts,-400);
    uint8_t em[SHELLY_EM_COUNT*2]={0};shelly_modbus_reading_t reading;
    mixed(em+(1013-SHELLY_EM_START)*2,-1234.5f);mixed(em+(1022-SHELLY_EM_START)*2,8.1f);
    mixed(em+(1042-SHELLY_EM_START)*2,8.2f);mixed(em+(1062-SHELLY_EM_START)*2,8.3f);
    assert(shelly_modbus_decode_em(em,sizeof(em),&reading));assert(reading.phases==3);
    near(reading.current_a[0],8.1);near(reading.current_a[1],8.2);near(reading.current_a[2],8.3);near(reading.active_power_w,-1234.5);
    assert(!shelly_modbus_decode_em(em,sizeof(em)-1,&reading));mixed(em+(1022-SHELLY_EM_START)*2,NAN);assert(!shelly_modbus_decode_em(em,sizeof(em),&reading));
    uint8_t em1[SHELLY_EM1_COUNT*2]={0};mixed(em1,230);mixed(em1+4,7.5);mixed(em1+8,1725);
    assert(shelly_modbus_decode_em1(em1,sizeof(em1),&reading));assert(reading.phases==1);near(reading.current_a[0],7.5);near(reading.active_power_w,1725);
    assert(!house_power_parse("{\"StatusSNS\":{\"ENERGY\":{\"Power\":\"nan\"}}}","tasmota","",&watts));
    assert(!house_power_parse("{\"StatusSNS\":{\"ENERGY\":{\"Power\":23}}}junk","tasmota","",&watts));
    /* Actual shape read from the user's Wattwaechter. Energy counters must
       never be mistaken for instantaneous signed grid power. */
    const char *grid_url="http://192.168.1.77/cm?cmnd=Status%2010";
    const char *grid_json="{\"StatusSNS\":{\"Time\":\"2026-09-21T12:38:12\",\"E320\":{\"E_in\":3663,\"E_out\":13955,\"Power\":-1943}}}";
    assert(house_power_parse(grid_json,"tasmota",grid_url,&watts));near(watts,-1943);
    assert(house_power_parse(grid_json,"tasmota","StatusSNS.E320.Power",&watts));near(watts,-1943);
    assert(house_power_parse("{\"StatusSNS\":{\"E320\":{\"Power\":450}}}","tasmota",grid_url,&watts));near(watts,450);
    assert(!house_power_parse("{\"StatusSNS\":{\"E320\":{\"E_in\":999,\"E_out\":888}}}","tasmota",grid_url,&watts));
    assert(!house_power_parse("{\"Power\":88,\"StatusSNS\":{\"E320\":{\"other\":{\"Power\":99}}}}","tasmota",grid_url,&watts));
    assert(!house_power_parse("{\"StatusSNS\":{\"E320\":{\"Power\":1},\"ENERGY\":{\"Power\":2}}}","tasmota",grid_url,&watts));
    assert(!house_power_parse("{\"StatusSNS\":{\"E320\":{\"Power\":null}}}","tasmota",grid_url,&watts));
    char query[256];assert(house_query_url("tasmota","unused-host",grid_url,query,sizeof(query)));assert(!strcmp(query,grid_url));
    assert(house_query_url("tasmota","192.168.1.77","StatusSNS.E320.Power",query,sizeof(query)));assert(!strcmp(query,grid_url));
    assert(!house_query_url("shelly_gen2","192.168.1.80","",query,sizeof(query)));
    assert(!house_query_url("tasmota","", "StatusSNS.E320.Power",query,sizeof(query)));
    assert(!house_query_url("tasmota","", "ftp://192.168.1.77/x",query,sizeof(query)));
    assert(!house_query_url("tasmota","", "http://user:pass@192.168.1.77/x",query,sizeof(query)));
    assert(!house_query_url("tasmota","", "http:///cm",query,sizeof(query)));
    assert(!house_query_url("tasmota","", "http://192.168.1.77/cm?cmnd=Status 10",query,sizeof(query)));
    assert(!house_query_url("tasmota","",grid_url,query,10));
    settings_defaults(&s);s.zero_feed_enabled=true;s.house_meter_host[0]=0;strcpy(s.house_power_path,grid_url);assert(settings_valid(&s));
    settings_defaults(&s);strcpy(s.wallbox_meter_type,"shelly_gen2");assert(!settings_valid(&s));strcpy(s.wallbox_meter_host,"192.168.1.34");assert(settings_valid(&s));
    settings_defaults(&s);strcpy(s.house_meter_type,"shelly_gen2");s.grid_guard_enabled=true;assert(settings_valid(&s));
    settings_defaults(&s);s.version=16;strcpy(s.house_meter_type,"shelly_gen1");strcpy(s.wallbox_meter_type,"shelly_gen1");strcpy(s.wallbox_meter_host,"192.168.1.34");s.zero_feed_enabled=true;s.grid_guard_enabled=true;
    assert(settings_decode(&s,(offsetof(settings_t,charge_plan_enabled)+3)&~(size_t)3,&out));assert(!strcmp(out.house_meter_type,"shelly_gen2"));assert(!strcmp(out.wallbox_meter_type,"shelly_gen2"));assert(!out.zero_feed_enabled&&!out.grid_guard_enabled&&!out.control_verified);
    assert(wifi_recovery_mode(false,false,false,500000,1)==EMS_WIFI_AP);
    assert(wifi_recovery_mode(true,true,false,500000,0)==EMS_WIFI_STA);
    assert(wifi_recovery_mode(true,false,false,300049,50)==EMS_WIFI_STA);
    assert(wifi_recovery_mode(true,false,false,300050,50)==EMS_WIFI_AP_STA);
    assert(wifi_recovery_mode(true,true,false,300051,0)==EMS_WIFI_STA);
    assert(wifi_recovery_mode(true,true,true,500000,0)==EMS_WIFI_AP);
    assert(wifi_recovery_mode(true,false,false,10,50)==EMS_WIFI_STA);
    settings_defaults(&s); s.zero_reserve_w=0; float actual[]={8,8,8};
    near(solar_current(&s,actual,1380),6);near(solar_current(&s,actual,-1380),10); near(solar_current(&s,actual,20000),0); near(solar_current(&s,actual,-20000),16);
    s.battery_protect=true; s.zero_feed_enabled=true; s.battery_reserve_soc=50;
    near(battery_solar_current(&s,actual,0,80,true,2000,true),8-2000.0f/690);
    near(battery_solar_current(&s,actual,0,80,true,0,false),8-4000.0f/690);
    near(battery_solar_current(&s,actual,-1380,49,true,0,true),10);
    near(battery_solar_current(&s,actual,-1380,50,true,0,true),10);
    near(battery_solar_current(&s,actual,-1380,80,false,0,true),10);
    near(battery_solar_current(&s,actual,NAN,80,true,0,true),0);
    assert(settings_valid(&s)); s.zero_reserve_w=100; assert(!settings_valid(&s));
    s.zero_reserve_w=0;
    near(battery_assisted_current(&s,actual,0,80,true,2000,true,true,true),8);
    near(battery_assisted_current(&s,actual,0,50,true,2000,true,true,true),8-2000.0f/690);
    near(battery_assisted_current(&s,actual,-1380,49,true,2000,true,true,false),10-2000.0f/690);
    near(battery_assisted_current(&s,actual,0,80,false,2000,true,true,false),8-2000.0f/690);
    near(battery_assisted_current(&s,actual,1380,80,true,2000,false,true,true),battery_solar_current(&s,actual,1380,80,true,2000,false));
    /* Before charge start, only unused capacity up to 4 kW is added. During
       charging, measured discharge above 4 kW is removed from availability. */
    float stopped_for_battery[]={0,0,0};
    near(battery_assisted_current(&s,stopped_for_battery,-4000,80,true,0,true,true,false),8000.0f/690);
    near(battery_assisted_current(&s,stopped_for_battery,0,80,true,2000,true,true,false),2000.0f/690);
    near(battery_assisted_current(&s,actual,0,80,true,5000,true,true,true),8-1000.0f/690);
    s.battery_assist_limit_w=2500;
    near(battery_assisted_current(&s,actual,0,80,true,5000,true,true,true),8-2500.0f/690);
    s.battery_assist_limit_w=4000;
    battery_buffer_t buffer={0};
    /* Battery support never starts charging, bridges up to fifteen minutes,
       then needs one stable PV-only minute before it receives a new budget. */
    near(battery_cloud_current(&buffer,&s,actual,0,80,true,2000,true,true,false,8,1000),8-2000.0f/690);
    near(battery_cloud_current(&buffer,&s,actual,0,80,true,2000,true,true,true,8,2000),8);
    assert(buffer.active && buffer.remaining_ms==EMS_BATTERY_BUFFER_MS);
    near(battery_cloud_current(&buffer,&s,actual,0,80,true,2000,true,true,true,8,181999),8);
    near(battery_cloud_current(&buffer,&s,actual,0,80,true,2000,true,true,true,8,901999),8);
    near(battery_cloud_current(&buffer,&s,actual,0,80,true,2000,true,true,true,8,902000),8-2000.0f/690);
    assert(!buffer.active);
    near(battery_cloud_current(&buffer,&s,actual,-1380,80,true,0,true,true,true,8,910000),10);
    for(int64_t t=911000;t<=970000;t+=1000)
        near(battery_cloud_current(&buffer,&s,actual,-1380,80,true,0,true,true,true,8,t),10);
    assert(buffer.started_at==0);
    near(battery_cloud_current(&buffer,&s,actual,0,80,true,5000,true,true,true,8,971000),8-1000.0f/690);
    battery_buffer_t limited={0};s.battery_cloud_limit_w=2500;
    near(battery_cloud_current(&limited,&s,actual,0,80,true,5000,true,true,true,8,971001),8-2500.0f/690);
    s.battery_cloud_limit_w=4000;
    /* Disabling the buffer removes battery power from availability but must
       use the normal PV deficit delay instead of a forced immediate stop. */
    pv_control_t transition={0};
    for(int64_t t=0;t<=60000;t+=1000) pv_control_step(&transition,true,12,8,16,t);
    float no_buffer=battery_cloud_current(&buffer,&s,actual,0,80,true,2000,true,false,true,8,972000);
    for(int64_t t=61000;t<=90000;t+=1000) {
        pv_control_step(&transition,true,no_buffer,8,16,t);
        assert(transition.target_a>=8);
    }
    pv_control_step(&transition,true,no_buffer,8,16,91000);
    assert(transition.target_a==0);
    float stopped[]={.2f,.2f,.2f};
    s.grid_limit_a=32; s.max_charge_a=16;
    float reported[3]; control_report(&s,stopped,0,0,false,reported);
    near(reported[0],49); /* stop remains asserted at zero current */
    puts("PASS: legacy settings, coils, UART/SDM, network meter schemas, Wattwaechter E320 URL, PV regulation, AP recovery policy");
}
