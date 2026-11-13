#include "http_display_estimate.h"
#include <assert.h>
#include <stdio.h>
#include <string.h>

static void near(float actual,float expected){assert(fabsf(actual-expected)<.02f);}
static void zero(http_display_estimate_t result){
    assert(result.valid&&result.watts==0);
    for(unsigned p=0;p<3;p++)assert(result.current[p]==0);
}

static void active_display_is_separate(void){
    shell_http_sample_t sample={.pilot_valid=true,.current_valid=true,.car_charging=true,
        .pilot_a=9,.current_a=3}; /* Delayed actual may still describe an older step. */
    shell_http_sample_t before=sample;
    const float voltage[3]={236,235,234};
    http_display_estimate_t result=http_display_estimate(&sample,1000,1001,true,false,8.7f,3,.25f,voltage,.99f);
    assert(result.valid);for(unsigned p=0;p<3;p++)near(result.current[p],8.75f);
    near(result.watts,8.75f*(236+235+234)*.99f);
    assert(!memcmp(&sample,&before,sizeof(sample))); /* Measured diagnostic state is never overwritten. */
    sample.current_a=35;
    http_display_estimate_t changed_delayed=http_display_estimate(&sample,1000,1001,true,false,8.7f,3,.25f,voltage,.99f);
    assert(!memcmp(&result,&changed_delayed,sizeof(result)));
    /* Instant estimate follows permission, not the requested setpoint. */
    result=http_display_estimate(&sample,1000,1001,true,false,16,3,.25f,voltage,.99f);
    near(result.current[0],8.75f);
    const float single_voltage[3]={240,NAN,NAN};
    result=http_display_estimate(&sample,1000,1001,true,false,8.7f,1,.25f,single_voltage,1);
    assert(result.valid&&result.current[1]==0&&result.current[2]==0);
    near(result.current[0],8.75f);near(result.watts,2100);
}

static void start_stop_and_fault(void){
    shell_http_sample_t sample={.pilot_valid=true,.current_valid=true,.car_charging=false,
        .pilot_a=12,.current_a=10};
    const float voltage[3]={230,230,230};
    /* Positive permission during preparation is not proof of an active charge. */
    zero(http_display_estimate(&sample,1000,1001,true,false,12,3,.25f,voltage,1));
    sample.car_charging=true;shell_http_sample_t before=sample;
    zero(http_display_estimate(&sample,1000,1001,false,false,12,3,.25f,voltage,1));
    zero(http_display_estimate(&sample,1000,1001,true,true,12,3,.25f,voltage,1));
    zero(http_display_estimate(&sample,1000,1001,true,false,0,3,.25f,voltage,1));
    assert(!memcmp(&sample,&before,sizeof(sample))); /* Reported actual10 A remains visible to safety logic. */
    /* Stop indication is immediate even if the last HTTP state is stale/absent. */
    zero(http_display_estimate(NULL,0,10000,false,false,12,3,NAN,NULL,NAN));
    zero(http_display_estimate(NULL,0,10000,true,true,12,3,NAN,NULL,NAN));
    sample.pilot_a=0;
    zero(http_display_estimate(&sample,1000,1001,true,false,12,3,-.5f,voltage,1));
}

static void missing_data_is_unknown(void){
    shell_http_sample_t sample={.pilot_valid=true,.car_charging=true,.pilot_a=12};
    float voltage[3]={230,230,230};
    assert(!http_display_estimate(&sample,1000,5000,true,false,12,3,.25f,voltage,1).valid);
    assert(!http_display_estimate(&sample,1001,1000,true,false,12,3,.25f,voltage,1).valid);
    assert(!http_display_estimate(&sample,0,1000,true,false,12,3,.25f,voltage,1).valid);
    assert(!http_display_estimate(NULL,1000,1001,true,false,12,3,.25f,voltage,1).valid);
    sample.pilot_valid=false;
    assert(!http_display_estimate(&sample,1000,1001,true,false,12,3,.25f,voltage,1).valid);
    sample.pilot_valid=true;
    const float bad[]={NAN,INFINITY,-1,33};
    for(unsigned i=0;i<sizeof(bad)/sizeof(*bad);i++){
        sample.pilot_a=bad[i];
        assert(!http_display_estimate(&sample,1000,1001,true,false,12,3,.25f,voltage,1).valid);
    }
    sample.pilot_a=12;
    for(unsigned p=0;p<3;p++){
        voltage[p]=NAN;
        assert(!http_display_estimate(&sample,1000,1001,true,false,12,3,.25f,voltage,1).valid);
        voltage[p]=230;
    }
    assert(!http_display_estimate(&sample,1000,1001,true,false,NAN,3,.25f,voltage,1).valid);
    assert(!http_display_estimate(&sample,1000,1001,true,false,12,2,.25f,voltage,1).valid);
    assert(!http_display_estimate(&sample,1000,1001,true,false,12,3,.9f,voltage,1).valid);
    assert(!http_display_estimate(&sample,1000,1001,true,false,12,3,.25f,voltage,0).valid);
    assert(!http_display_estimate(&sample,1000,1001,true,false,12,3,.25f,voltage,1.1f).valid);
}

static void estimate_ceiling(void){
    shell_http_sample_t sample={.pilot_valid=true,.car_charging=true,.pilot_a=16};
    const float voltage[3]={230,230,230};
    http_display_estimate_t result=http_display_estimate(&sample,1000,1001,true,false,16,3,.25f,voltage,1);
    near(result.current[0],15.75f);near(result.watts,10867.5f);
    result=http_display_estimate(&sample,1000,1001,true,false,16,3,-.5f,voltage,1);
    near(result.current[0],16);near(result.watts,11040);
    sample.pilot_a=32;
    result=http_display_estimate(&sample,1000,1001,true,false,16,3,.25f,voltage,1);
    assert(result.valid&&result.current[0]<=16);
}

int main(void){
    active_display_is_separate();start_stop_and_fault();missing_data_is_unknown();estimate_ceiling();
    puts("PASS: separate HTTP display estimate, voltage-based watts, start/stop/fault, stale unknown and bounded current; measured input unchanged");
}
