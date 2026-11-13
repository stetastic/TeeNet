#include "shell_http_runtime.h"
#include <assert.h>
#include <stdio.h>
#define CONFIG_TEE_HTTP_CONTROL_TRIAL 1


static shell_http_sample_t parse(const char *body){
    cJSON *json=cJSON_Parse(body);assert(json);shell_http_sample_t s;shell_http_sample_parse(json,&s);cJSON_Delete(json);return s;
}
static void parser(void){
    shell_http_sample_t s=parse("{\"limit\":{\"current\":8.75},\"chargingRate\":\"Charging rate: 5.9kw 3 phase [8.5A 0.95pf ] \"}");
    assert(s.pilot_valid&&s.current_valid&&s.pilot_a==8.75f&&s.current_a==8.5f);
    const char *invalid[]={"{}","{\"limit\":{\"current\":null}}","{\"limit\":{\"current\":\"8\"}}","{\"limit\":{\"current\":-1}}","{\"limit\":{\"current\":99}}"};
    for(unsigned i=0;i<sizeof(invalid)/sizeof(*invalid);i++){s=parse(invalid[i]);assert(!s.pilot_valid);}
    const char *bad[]={"Charging rate: nankw 3 phase [8A ]","Charging rate: 1kw 4 phase [8A ]","Charging rate: 1kw 3 phase [nanA ]","Charging rate: 1kw 3 phase [-1A ]","Charging rate: 1kw 3 phase [3A"};
    for(unsigned i=0;i<sizeof(bad)/sizeof(*bad);i++){char body[200];snprintf(body,sizeof(body),"{\"chargingRate\":\"%s\"}",bad[i]);s=parse(body);assert(!s.current_valid);}
}
static void gates(void){
    shell_http_control_t c;shell_http_sample_t s={.pilot_valid=true,.car_charging=true,.pilot_a=12,.current_valid=true,.current_a=1};float a[3],b[3];
    shell_http_trial_arm(&c,1000);shell_http_control_t original=c;
    shell_http_trial_report(&c,&s,1000,1000,9,3,32,8.7f,16,false,a);
    assert(!memcmp(&c,&original,sizeof(c))); /* Status GET is pure. */
    shell_http_trial_report(&c,&s,1000,1000,9,3,32,8.7f,16,true,a);
    s.current_a=38;shell_http_control_t duplicate=original;
    shell_http_trial_report(&duplicate,&s,1000,1000,9,3,32,8.7f,16,true,b);
    assert(!memcmp(a,b,sizeof(a))); /* Delayed measured current never affects command. */
    shell_http_trial_report(&c,&s,1100,1100,9,3,32,8.7f,16,true,a);
    float previous=a[0];unsigned steps=c.steps;
    shell_http_trial_report(&c,&s,1100,1200,9,3,32,8.7f,16,true,a);
    assert(a[0]==previous&&c.steps==steps); /* Re-reading a snapshot cannot accumulate correction. */
    shell_http_trial_report(&c,&s,1100,1100,0,1,32,8.7f,16,true,a);assert(a[0]==49&&a[1]==0&&a[2]==0);
    shell_http_trial_report(&c,&s,1200,16000,9,3,32,8.7f,16,true,a);assert(c.fault&&a[0]==49);
    c.lease_until=99999;shell_http_trial_report(&c,&s,16000,16000,9,3,32,8.7f,16,true,a);assert(c.fault&&a[0]==49); /* No renewal clears a fault. */
    shell_http_trial_arm(&c,1000);shell_http_trial_report(&c,&s,1000,5000,9,3,32,8.7f,16,true,a);assert(c.fault&&a[0]==49);
    const float invalid[]={NAN,INFINITY,-1,33};
    for(unsigned i=0;i<sizeof(invalid)/sizeof(*invalid);i++){
        shell_http_trial_arm(&c,1000);s.pilot_a=invalid[i];
        shell_http_trial_report(&c,&s,1000,1000,9,3,32,8.7f,16,true,a);assert(c.fault&&a[0]==49);
    }
}
static void consistent_goal(void){
    const float targets[]={8.7f,9,10,12,14,15.9f,16};
    const float biases[]={-.5f,0,.25f,.8f};
    for(unsigned phases=1;phases<=3;phases+=2)for(unsigned n=0;n<sizeof(targets)/sizeof(*targets);n++)
    for(unsigned b=0;b<sizeof(biases)/sizeof(*biases);b++){
        float target=targets[n],goal=fminf(16,fmaxf(8.7f,target+biases[b]));
        for(unsigned k=0;k<3;k++){
            float offered=goal+(k==0?-1:k==1?0:1);
            shell_http_control_t c;shell_http_trial_arm(&c,1000);c.pilot_bias_a=biases[b];c.normal_offset_a=.8f;
            shell_http_sample_t sample={.pilot_valid=true,.car_charging=true,.pilot_a=offered};
            float got[3],other[3];
            shell_http_control_t copy=c;copy.normal_offset_a=1.9f;
            shell_http_trial_report(&c,&sample,1000,1000,target,phases,32,8.7f,16,true,got);
            shell_http_trial_report(&copy,&sample,1000,1000,target,phases,32,8.7f,16,true,other);
            assert(!c.fault&&!copy.fault&&c.stage==copy.stage); /* Neutral compensation cannot shift the pilot goal or its status. */
            assert(fabsf(other[0]-got[0]-1.1f)<.00001f);
            if(k==0)assert(fabsf(got[0]-31.8f)<.00001f&&c.stage==HTTP_TRIAL_WAIT);
            if(k==1)assert(got[0]==32.8f&&c.stage==HTTP_TRIAL_BALANCE);
            if(k==2)assert(fabsf(got[0]-33.8f)<.00001f&&c.stage==HTTP_TRIAL_WAIT);
            for(unsigned p=1;p<3;p++){
                assert(got[p]==(p<phases?got[0]:0));
                assert(other[p]==(p<phases?other[0]:0));
            }
        }
    }
    /* At 8.7 A, 8.95 A pilot is simultaneously neutral and balanced. The
       compensation holds the virtual-current neutral; it is not a goal offset. */
    shell_http_control_t c;shell_http_trial_arm(&c,1000);float out[3];
    shell_http_sample_t s={.pilot_valid=true,.car_charging=true,.pilot_a=8.95f};
    shell_http_trial_report(&c,&s,1000,1000,8.7f,3,32,8.7f,16,true,out);
    assert(out[0]==32.8f&&c.stage==HTTP_TRIAL_BALANCE);
    /* Saturation must not keep trying to grant more than the 16-A pilot cap. */
    s.pilot_a=16;shell_http_trial_report(&c,&s,1100,1100,16,3,32,8.7f,16,true,out);
    assert(out[0]==32.8f&&c.stage==HTTP_TRIAL_BALANCE);
}
static void neutral_compensation(void){
    const float offsets[]={0,.8f,2};
    const float errors[]={-.24f,0,.24f};
    float out[3];
    for(unsigned phases=1;phases<=3;phases+=2)for(unsigned i=0;i<sizeof(offsets)/sizeof(*offsets);i++){
        shell_http_control_t c;shell_http_trial_arm(&c,1000);c.normal_offset_a=offsets[i];
        shell_http_sample_t s={.pilot_valid=true,.car_charging=true};
        for(unsigned e=0;e<sizeof(errors)/sizeof(*errors);e++){
            s.pilot_a=12.25f+errors[e];
            shell_http_trial_report(&c,&s,1000,1000,12,phases,32,8.7f,16,true,out);
            assert(!c.fault&&c.stage==HTTP_TRIAL_BALANCE);
            float expected=32+offsets[i]+(phases==3?errors[e]:0);
            assert(fabsf(out[0]-expected)<.00001f); /*3p reacts to +/- .24;1p still holds neutral. */
        }
        /* START headroom is retained until the car acknowledges charging. */
        s.car_charging=false;
        shell_http_trial_report(&c,&s,1000,1000,12,phases,32,8.7f,16,true,out);
        assert(!c.fault&&c.stage==HTTP_TRIAL_START&&out[0]==24);
        s.car_charging=true;s.pilot_a=0;
        shell_http_trial_report(&c,&s,1000,1000,12,phases,32,8.7f,16,true,out);
        assert(!c.fault&&c.stage==HTTP_TRIAL_START&&out[0]==24);
        shell_http_trial_report(&c,&s,1000,1000,0,phases,32,8.7f,16,true,out);
        assert(!c.fault&&c.stage==HTTP_TRIAL_STOPPED&&out[0]==49);
        for(unsigned p=1;p<3;p++)assert(out[p]==(p<phases?49:0));
    }
    const float invalid[]={NAN,INFINITY,-INFINITY,-.01f,2.01f};
    for(unsigned i=0;i<sizeof(invalid)/sizeof(*invalid);i++){
        shell_http_control_t c;shell_http_trial_arm(&c,1000);c.normal_offset_a=invalid[i];
        shell_http_sample_t s={.pilot_valid=true,.car_charging=true,.pilot_a=12};
        shell_http_control_t original=c;
        shell_http_trial_report(&c,&s,1000,1000,12,3,32,8.7f,16,false,out);
        assert(!memcmp(&c,&original,sizeof(c))&&out[0]==49);
        shell_http_trial_report(&c,&s,1000,1000,12,3,32,8.7f,16,true,out);
        assert(c.fault&&c.stage==HTTP_TRIAL_FAULT&&out[0]==49&&out[1]==49&&out[2]==49);
    }
}
static void start_confirmation(void){
    shell_http_control_t c;shell_http_trial_arm(&c,1000);float out[3];
    shell_http_sample_t sample={.pilot_valid=true,.pilot_a=8,.car_charging=false};
    shell_http_trial_report(&c,&sample,1000,1000,8.7f,3,32,8.7f,16,true,out);
    assert(out[0]==24&&c.stage==HTTP_TRIAL_START); /* Permission alone must not revoke START headroom. */
    sample.car_charging=true;
    shell_http_trial_report(&c,&sample,1100,1100,8.7f,3,32,8.7f,16,true,out);
    assert(out[0]<32&&c.stage!=HTTP_TRIAL_START);
    sample=parse("{\"limit\":{\"current\":8},\"transaction\":{\"statusCar\":\"Charging\"}}");
    assert(sample.car_charging&&sample.pilot_valid&&!sample.current_valid);
}

/* Run the extracted production composition rather than a parallel test rewrite. */
static void uart_attempt(shell_http_control_t *state,manual_report_filter_t *filter,const settings_t *s,
    shell_http_sample_t sample,int64_t sample_at,int64_t report_now,float request,bool current_reply,
    bool sent,int64_t wallbox_at,bool safety_stop,float currents[3]){
    shell_http_runtime_t http_regulator={0};
    shell_http_control_t shell_http_trial=*state;manual_report_filter_t manual_filter=*filter;settings_t settings=*s;
    struct {shell_http_sample_t http_control;} evcc_shell={sample};int64_t evcc_shell_at=sample_at;
    float actual_a[3]={0},house_power_w=0;bool house_ok=false;
    uint8_t req[8]={settings.wallbox_address,current_reply?3:4,0x50,0x0c,0,6,0,0};
#include "shell_http_uart_actual.inc"
    if(safety_stop)for(unsigned p=0;p<3;p++)currents[p]=settings.grid_limit_a+settings.max_charge_a+1;
    if(sent&&current_reply){
#include "shell_http_uart_sent_actual.inc"
    }
    (void)http_reply_trigger;
    *state=shell_http_trial;*filter=manual_filter;
}
static void uart_reply(shell_http_control_t *state,manual_report_filter_t *filter,const settings_t *s,
    shell_http_sample_t sample,int64_t sample_at,int64_t report_now,float request,bool current_reply,float currents[3]){
    uart_attempt(state,filter,s,sample,sample_at,report_now,request,current_reply,true,report_now+5,false,currents);
}
static settings_t trial_settings(unsigned phases){
    settings_t s;settings_defaults(&s);s.mode=MODE_MANUAL;s.charge_phases=phases;return s;
}
static void near(float got,float want){if(fabsf(got-want)>=.0001f)printf("near: got %.6f expected %.6f\n",got,want);assert(fabsf(got-want)<.0001f);}
static void startup_handover(void){
    settings_t s=trial_settings(3);shell_http_control_t c;manual_report_filter_t f={0};float out[3];
    shell_http_trial_arm(&c,1000);
    shell_http_sample_t sample={.pilot_valid=true,.pilot_a=0,.car_charging=false};
    uart_reply(&c,&f,&s,sample,1000,1000,8.7f,true,out);near(out[0],24);assert(c.startup_filter_starting);
    sample.pilot_a=8;
    shell_http_control_t saved=c;manual_report_filter_t saved_filter=f;
    shell_http_trial_report(&c,&sample,2000,2000,8.7f,3,32,8.7f,16,false,out);
    near(out[0],24);assert(!memcmp(&c,&saved,sizeof(c))&&!memcmp(&f,&saved_filter,sizeof(f)));
    uart_reply(&c,&f,&s,sample,2000,2000,8.7f,false,out);
    assert(!memcmp(&c,&saved,sizeof(c))&&!memcmp(&f,&saved_filter,sizeof(f))); /* Other UART reads are pure too. */
    uart_reply(&c,&f,&s,sample,3000,3000,8.7f,true,out);
    near(out[0],24);assert(c.startup_filter_starting&&c.stage==HTTP_TRIAL_START);
    sample.car_charging=true;
    uart_reply(&c,&f,&s,sample,5000,5000,8.7f,true,out);
    near(out[0],24);assert(c.startup_filter_starting&&c.stage==HTTP_TRIAL_START);
    sample.pilot_a=9.2366667f;
    uart_reply(&c,&f,&s,sample,7000,7000,8.7f,true,out);
    near(out[0],32.8f);assert(!c.startup_filter_starting&&!c.startup_filter_recovery); /* Fresh pilot above minimum permits the one-time handover. */
    uart_reply(&c,&f,&s,sample,9000,9000,8.7f,true,out);near(out[0],32.84f); /* No repeated reseed. */
    sample.pilot_a=8.95f;
    uart_reply(&c,&f,&s,sample,11000,11000,8.7f,true,out);near(out[0],32.8f);
    for(unsigned p=1;p<3;p++)near(out[p],out[0]);
}
static void startup_scope_and_boundaries(void){
    for(unsigned phases=1;phases<=3;phases+=2){
        settings_t s=trial_settings(phases);shell_http_control_t c;manual_report_filter_t f={0};float out[3];
        shell_http_trial_arm(&c,1000);
        shell_http_sample_t sample={.pilot_valid=true,.pilot_a=0,.car_charging=false};
        uart_reply(&c,&f,&s,sample,1000,1000,8.7f,true,out);
        if(phases==1){
            near(out[0],32);assert(c.startup_filter_starting&&!c.startup_filter_recovery);
            const float pilots[]={8,8.95f,12,8.7f};sample.car_charging=true;
            for(unsigned i=0;i<4;i++){
                int64_t now=3000+2000*i;sample.pilot_a=pilots[i];
                shell_http_control_t legacy=c;manual_report_filter_t legacy_filter=f;float expected[3];
                shell_http_trial_report(&legacy,&sample,now,now,8.7f,1,32,8.7f,16,true,expected);
                manual_report_smooth(&legacy_filter,&s,8.7f,now,expected);
                uart_reply(&c,&f,&s,sample,now,now,8.7f,true,out);
                assert(!memcmp(out,expected,sizeof(out))&&!memcmp(&f,&legacy_filter,sizeof(f)));
                assert(out[1]==0&&out[2]==0);
            }
            assert(!c.startup_charging_ready&&c.startup_charging_since==3000); /* Exact six-second receipt is insufficient. */
            sample.pilot_a=12;
            uart_reply(&c,&f,&s,sample,11000,11000,8.7f,true,out);
            assert(c.startup_charging_ready&&!c.startup_filter_starting);near(out[0],32.8f);
            assert(out[1]==0&&out[2]==0);
        }else{
            sample.car_charging=true;sample.pilot_a=12;
            uart_reply(&c,&f,&s,sample,3000,3000,8.7f,true,out);near(out[0],32.8f); /* Cap first reduction at neutral. */
            uart_reply(&c,&f,&s,sample,5000,5000,8.7f,true,out);near(out[0],33.3f); /* Existing far-error .5 step retained. */
        }
    }
    const float offsets[]={0,.8f,2};
    for(unsigned i=0;i<3;i++){
        settings_t s=trial_settings(3);s.current_offset_a=offsets[i];
        shell_http_control_t c;manual_report_filter_t f={0};float out[3];shell_http_trial_arm(&c,1000);c.normal_offset_a=offsets[i];
        shell_http_sample_t sample={.pilot_valid=true,.pilot_a=0};
        uart_reply(&c,&f,&s,sample,1000,1000,8.7f,true,out);near(out[0],24);
        sample.car_charging=true;sample.pilot_a=8.95f;
        uart_reply(&c,&f,&s,sample,3000,3000,8.7f,true,out);near(out[0],32+offsets[i]);
    }
    for(unsigned phases=1;phases<=3;phases+=2){
        settings_t s=trial_settings(phases);shell_http_control_t c={0};manual_report_filter_t f={0},legacy_filter={0};float out[3],expected[3],actual[3]={0};
        shell_http_sample_t sample={.pilot_valid=true,.pilot_a=8};
        for(int64_t now=1000;now<=5000;now+=2000){
            control_report(&s,actual,8.7f,0,false,expected);manual_report_smooth(&legacy_filter,&s,8.7f,now,expected);
            uart_reply(&c,&f,&s,sample,now,now,8.7f,true,out);
            assert(!memcmp(out,expected,sizeof(out))&&!memcmp(&f,&legacy_filter,sizeof(f))); /* Unarmed normal path unchanged. */
        }
    }
}
static void startup_cancel_and_stop(void){
    for(unsigned action=0;action<5;action++){
        settings_t s=trial_settings(3);shell_http_control_t c;manual_report_filter_t f={0};float out[3];shell_http_trial_arm(&c,1000);
        shell_http_sample_t sample={.pilot_valid=true,.pilot_a=0};
        uart_reply(&c,&f,&s,sample,1000,1000,8.7f,true,out);
        sample.pilot_a=8.7f;sample.car_charging=true;uart_reply(&c,&f,&s,sample,3000,3000,8.7f,true,out);assert(c.startup_filter_recovery);
        if(action<2){
            float target=action==0?12:8.7f;if(action==1)s.charge_phases=1;
            shell_http_control_t legacy=c;manual_report_filter_t legacy_filter=f;float expected[3];
            shell_http_trial_report(&legacy,&sample,5000,5000,target,s.charge_phases,32,8.7f,16,true,expected);
            manual_report_smooth(&legacy_filter,&s,target,5000,expected);
            uart_reply(&c,&f,&s,sample,5000,5000,target,true,out);
            assert(!c.startup_filter_starting&&!c.startup_filter_recovery);
            assert(!memcmp(out,expected,sizeof(out))&&!memcmp(&f,&legacy_filter,sizeof(f))); /* No handover on target/phase change. */
        }else{
            int64_t now=action==4?16000:7000,at=action==3?3000:now;float target=action==2?0:8.7f;
            shell_http_control_t saved=c;manual_report_filter_t saved_filter=f;
            uart_reply(&c,&f,&s,sample,at,now,target,false,out);
            assert(!memcmp(&c,&saved,sizeof(c))&&!memcmp(&f,&saved_filter,sizeof(f)));
            uart_reply(&c,&f,&s,sample,at,now,target,true,out);
            for(unsigned p=0;p<3;p++)near(out[p],49);
            assert(!f.active&&!c.startup_filter_starting&&!c.startup_filter_recovery);
        }
    }
}
static void start_permission_and_car_watchdog(void){
    const float targets[]={8.7f,12,16};
    for(unsigned i=0;i<3;i++){
        settings_t s=trial_settings(3);shell_http_control_t c;manual_report_filter_t f={0};float out[3];
        shell_http_trial_arm(&c,1000);c.lease_until=100000;
        shell_http_sample_t sample={.pilot_valid=true};
        uart_reply(&c,&f,&s,sample,1000,1000,targets[i],true,out);near(out[0],24);
        sample.pilot_a=8;
        uart_reply(&c,&f,&s,sample,2800,3000,targets[i],true,out);
        float goal=fminf(16,targets[i]+.25f);
        near(out[0],24);assert(c.stage==HTTP_TRIAL_START&&c.waiting_since==1000);
        /* Fresh permission and a balanced pilot do not renew the car timeout. */
        sample.pilot_a=goal;
        for(int64_t now=5000;now<=39000;now+=2000){
            uart_reply(&c,&f,&s,sample,now,now,targets[i],true,out);
            near(out[0],24);assert(!c.fault&&c.stage==HTTP_TRIAL_START&&c.waiting_since==1000);
        }
        uart_reply(&c,&f,&s,sample,41001,41001,targets[i],true,out);
        assert(c.fault&&c.stage==HTTP_TRIAL_FAULT&&!c.coast_active&&!f.active);
        for(unsigned p=0;p<3;p++)near(out[p],49);
    }
    /* Recorded failed184733 startup:8 A/NotCharging at40.000/42.199 s must
       retain24 on the40.903/42.966 s current replies. Restoring7c requires
       Charging at44.413 s alone now keeps START on45.023 s until the hybrid guard qualifies. This asserts the output
       sequence, not a counterfactual prediction of the physical current. */
    settings_t s=trial_settings(3);shell_http_control_t c;manual_report_filter_t f={0};float out[3];
    shell_http_trial_arm(&c,22000);c.lease_until=100000;
    shell_http_sample_t sample={.pilot_valid=true};
    uart_reply(&c,&f,&s,sample,22177,22275,8.7f,true,out);near(out[0],24);
    sample.pilot_a=8;
    uart_reply(&c,&f,&s,sample,40000,40903,8.7f,true,out);near(out[0],24);
    uart_reply(&c,&f,&s,sample,42199,42966,8.7f,true,out);near(out[0],24);
    sample.car_charging=true;
    uart_reply(&c,&f,&s,sample,44413,45023,8.7f,true,out);near(out[0],24);
    assert(c.stage==HTTP_TRIAL_START&&!c.fault);
}

static void prepare_coast(shell_http_control_t *c,manual_report_filter_t *f,settings_t *s,float out[3]){
    *s=trial_settings(3);*f=(manual_report_filter_t){0};shell_http_trial_arm(c,1000);c->lease_until=1000000;
    shell_http_sample_t sample={.pilot_valid=true,.car_charging=true,.pilot_a=12.320139f};
    uart_reply(c,f,s,sample,1000,1000,12,true,out);near(out[0],32.8f);
    for(int64_t now=3000;now<=9000;now+=2000)uart_reply(c,f,s,sample,now-100,now,8.7f,true,out);
    near(out[0],34.8f);assert(c->coast_reduction_sent&&!c->coast_active);
}
static void coast_observed_uart_trajectory(void){
    shell_http_control_t c;manual_report_filter_t f;settings_t s;float out[3];prepare_coast(&c,&f,&s,out);
    shell_http_sample_t sample={.pilot_valid=true,.car_charging=true,.pilot_a=11.136667f};
    uart_reply(&c,&f,&s,sample,10800,11000,8.7f,true,out);
    near(out[0],32.8f);assert(c.coast_active&&!c.coast_waiting_sent&&c.coast_until==19005);
    int64_t deadline=c.coast_until;
    shell_http_control_t saved=c;manual_report_filter_t saved_filter=f;
    sample.pilot_a=10.4f;
    uart_reply(&c,&f,&s,sample,12000,12000,8.7f,false,out);
    assert(!memcmp(&c,&saved,sizeof(c))&&!memcmp(&f,&saved_filter,sizeof(f))); /* Other reads cannot renew the hold. */
    sample.pilot_a=11.136667f;
    uart_reply(&c,&f,&s,sample,10800,13000,8.7f,true,out);near(out[0],32.8f);assert(c.coast_until==deadline);
    uart_reply(&c,&f,&s,sample,14800,15000,8.7f,true,out);near(out[0],32.8f);assert(c.coast_until==deadline);
    /* The old6-second edge must not resume on the last high sample. */
    uart_reply(&c,&f,&s,sample,16800,17000,8.7f,true,out);near(out[0],32.8f);assert(c.coast_active);
    sample.pilot_a=9.174f;
    uart_reply(&c,&f,&s,sample,18800,19000,8.7f,true,out);near(out[0],32.8f);
    uart_reply(&c,&f,&s,sample,18800,21000,8.7f,true,out);near(out[0],32.8f);assert(c.coast_active);
    uart_reply(&c,&f,&s,sample,21900,22000,8.7f,true,out);near(out[0],32.82f);assert(!c.coast_active); /* Unchanged .02-A/s fine ramp after coast. */
    /* These are output-trajectory regressions, not a predicted physical response. */
}
static void coast_restart_recovery_and_delivery(void){
    for(unsigned mode=0;mode<3;mode++){
        shell_http_control_t c;manual_report_filter_t f;settings_t s;float out[3];prepare_coast(&c,&f,&s,out);
        shell_http_sample_t sample={.pilot_valid=true,.car_charging=true,.pilot_a=11.136667f};
        uart_attempt(&c,&f,&s,sample,10800,11000,8.7f,true,mode==1,11005,mode==1,out);
        assert(c.coast_active&&c.coast_waiting_sent&&c.coast_until==0); /* Failed write or substitutedSTOP never starts the clock. */
        if(mode==2){
            shell_http_trial_transmitted(&c,10999,c.coast_trigger_at,11005,8.7f,3,32.8f,49,out);
            assert(c.coast_waiting_sent);
        }
        uart_reply(&c,&f,&s,sample,12800,13000,8.7f,true,out);
        assert(!c.coast_waiting_sent&&c.coast_until==21005);near(out[0],32.8f);
        sample.pilot_a=10.4f;
        uart_reply(&c,&f,&s,sample,14800,15000,8.7f,true,out);near(out[0],32.8f);assert(c.coast_until==21005);
        uart_reply(&c,&f,&s,sample,14800,17000,8.7f,true,out);assert(c.coast_until==21005); /* Same near-zone hold cannot renew. */
        /* An actual low pilot immediately releases the coast, before its deadline. */
        sample.pilot_a=8.425333f;
        uart_reply(&c,&f,&s,sample,17800,18000,8.7f,true,out);near(out[0],32.275333f);
        assert(!c.coast_active&&!c.coast_waiting_sent&&!c.coast_reduction_sent);
        assert(c.coast_near_used&&!c.coast_bulk_used); /* Undershoot releases, never refills. */
    }
    shell_http_control_t c;manual_report_filter_t f;settings_t s;float out[3];prepare_coast(&c,&f,&s,out);
    shell_http_sample_t sample={.pilot_valid=true,.car_charging=true,.pilot_a=11.136667f};
    uart_reply(&c,&f,&s,sample,10800,11000,8.7f,true,out);
    /* Freshness and post-deadline reception are independent requirements. */
    uart_reply(&c,&f,&s,sample,19005,21000,8.7f,true,out);assert(c.coast_active);
    uart_reply(&c,&f,&s,sample,21001,23000,8.7f,true,out);assert(!c.coast_active);near(out[0],33.3f);
}
static void coast_scope_resets_and_faults(void){
    for(unsigned action=0;action<7;action++){
        shell_http_control_t c;manual_report_filter_t f;settings_t s;float out[3];prepare_coast(&c,&f,&s,out);
        shell_http_sample_t sample={.pilot_valid=true,.car_charging=true,.pilot_a=11.136667f};
        uart_reply(&c,&f,&s,sample,10800,11000,8.7f,true,out);assert(c.coast_active);
        float target=8.7f;int64_t at=13000,now=13000;
        if(action==0)target=0;
        if(action==1)target=12;
        if(action==2)s.charge_phases=1;
        if(action==3)at=9000; /* Stale at exactly4s fails closed. */
        if(action==4)c.lease_until=now;
        if(action==5)sample.pilot_valid=false;
        if(action==6){sample.pilot_a=0;sample.car_charging=false;}
        uart_reply(&c,&f,&s,sample,at,now,target,true,out);
        assert(!c.coast_active&&!c.coast_waiting_sent&&!c.coast_reduction_sent);
        if(action==0||action==3||action==4||action==5)near(out[0],49);
        if(action==2)assert(out[1]==0&&out[2]==0);
    }
    /* A small change or an untransmitted reduction cannot start a coast. */
    shell_http_control_t c;manual_report_filter_t f;settings_t s;float out[3];prepare_coast(&c,&f,&s,out);
    shell_http_sample_t sample={.pilot_valid=true,.car_charging=true,.pilot_a=12.13f};
    uart_reply(&c,&f,&s,sample,10800,11000,8.7f,true,out);assert(!c.coast_active);
    sample.pilot_a=12.10f;uart_reply(&c,&f,&s,sample,12800,13000,8.7f,true,out);assert(c.coast_active); /* Cumulative meaningful decline. */
    prepare_coast(&c,&f,&s,out);c.coast_reduction_sent=false;sample.pilot_a=11.136667f;
    uart_attempt(&c,&f,&s,sample,10800,11000,8.7f,true,false,11005,false,out);assert(!c.coast_active);
}
static void one_phase_recorded_handover(void){
    /*190007: the first Charging receipt at169.228 preceded established flow.
      The new handover cannot occur on170.128; continuous receipts through
     175.829 allow it on176.336. No measured current is supplied to control. */
    settings_t s=trial_settings(1);shell_http_control_t c;manual_report_filter_t f={0};float out[3];
    shell_http_trial_arm(&c,137000);c.lease_until=1000000;
    shell_http_sample_t sample={.pilot_valid=true};
    const int64_t starting[]={137741,139811,141075,143147,145219,147291,149363,151435,153507,155614,157687,159759,161839,163896,165956,168016};
    for(unsigned i=0;i<sizeof(starting)/sizeof(*starting);i++){
        uart_reply(&c,&f,&s,sample,starting[i]-100,starting[i],8.7f,true,out);
        assert(c.stage==HTTP_TRIAL_START&&c.startup_filter_starting&&out[1]==0&&out[2]==0);
    }
    near(out[0],24);
    sample.pilot_a=8;sample.car_charging=true;
    const int64_t receipts[]={169228,171429,173629};
    const int64_t replies[]={170128,172188,174244};
    for(unsigned i=0;i<3;i++){
        uart_reply(&c,&f,&s,sample,receipts[i],replies[i],8.7f,true,out);
        assert(!c.startup_charging_ready&&c.startup_filter_starting&&out[0]<27&&out[1]==0&&out[2]==0);
    }
    shell_http_control_t saved=c;manual_report_filter_t saved_filter=f;
    sample.pilot_a=12.9502f;
    uart_reply(&c,&f,&s,sample,175829,176336,8.7f,false,out);
    assert(!memcmp(&c,&saved,sizeof(c))&&!memcmp(&f,&saved_filter,sizeof(f)));
    uart_reply(&c,&f,&s,sample,175829,176336,8.7f,true,out);
    near(out[0],32.8f);assert(c.startup_charging_ready&&!c.startup_filter_starting&&out[1]==0&&out[2]==0);
    uart_reply(&c,&f,&s,sample,178029,178396,8.7f,true,out);
    assert(out[0]>32.8f&&out[0]<=33.83f&&out[1]==0&&out[2]==0); /* Existing ramp after one reseed. */
}
static void one_phase_guard_boundaries(void){
    for(unsigned reset=0;reset<10;reset++){
        settings_t s=trial_settings(1);shell_http_control_t c;manual_report_filter_t f={0};float out[3];
        shell_http_trial_arm(&c,1000);c.lease_until=100000;
        shell_http_sample_t sample={.pilot_valid=true};
        uart_reply(&c,&f,&s,sample,1000,1000,8.7f,true,out);
        sample.pilot_a=8; /* Positive permission before Charging cannot begin the guard. */
        uart_reply(&c,&f,&s,sample,2000,2000,8.7f,true,out);assert(!c.startup_charging_since);
        sample.car_charging=true;
        for(int64_t now=3000;now<=7000;now+=2000)uart_reply(&c,&f,&s,sample,now,now,8.7f,true,out);
        assert(c.startup_charging_since==3000&&!c.startup_charging_ready);
        int64_t at=9000,now=9000;float target=8.7f;
        if(reset==0)sample.car_charging=false;
        if(reset==1)sample.pilot_a=0;
        if(reset==2)at=now=11000; /* Fresh present sample, but exactly4s receipt gap. */
        if(reset==3)at=6000; /* Regressed reception cannot credit the old sequence. */
        if(reset==4)target=0;
        if(reset==5)target=12;
        if(reset==6)s.charge_phases=3;
        if(reset==7)at=5000; /* Current snapshot stale at4s. */
        if(reset==8)sample.pilot_valid=false;
        if(reset==9)c.lease_until=now;
        uart_reply(&c,&f,&s,sample,at,now,target,true,out);
        assert(!c.startup_charging_ready);
        if(reset==2||reset==3||reset==5)assert(c.startup_charging_since==at);
        else assert(!c.startup_charging_since&&!c.startup_charging_sample);
        if(reset==4||reset>=7)near(out[0],49);
        if(s.charge_phases==1)assert(out[1]==0&&out[2]==0);
    }
    settings_t s=trial_settings(1);shell_http_control_t c;manual_report_filter_t f={0};float out[3];
    shell_http_trial_arm(&c,1000);c.lease_until=100000;
    shell_http_sample_t sample={.pilot_valid=true};
    uart_reply(&c,&f,&s,sample,1000,1000,8.7f,true,out);sample.pilot_a=8;sample.car_charging=true;
    for(int64_t now=2000;now<=8000;now+=2000)uart_reply(&c,&f,&s,sample,now,now,8.7f,true,out);
    assert(!c.startup_charging_ready); /* Strictly after deadline, not at it. */
    uart_reply(&c,&f,&s,sample,8000,10000,8.7f,true,out);assert(!c.startup_charging_ready&&c.startup_filter_starting);
    uart_reply(&c,&f,&s,sample,10001,10001,8.7f,true,out);assert(c.startup_charging_ready&&!c.startup_filter_starting);near(out[0],31.85f);
    sample.car_charging=false;uart_reply(&c,&f,&s,sample,11000,11000,8.7f,true,out);
    assert(!c.startup_charging_ready&&!c.startup_charging_since&&c.startup_filter_starting);
}
static void coast_bounded_recorded_descent(void){
    /*19000716->8.7: six repeated holds consumed63s. Replay its pilot declines
      through actual UART composition; only the first bulk and first near-goal
      decline can now start a deadline. This does not predict physical response. */
    settings_t s=trial_settings(3);shell_http_control_t c;manual_report_filter_t f={0};float out[3];
    shell_http_trial_arm(&c,1000);c.lease_until=1000000;
    shell_http_sample_t sample={.pilot_valid=true,.car_charging=true,.pilot_a=16};
    uart_reply(&c,&f,&s,sample,1000,1000,16,true,out);
    for(int64_t now=3000;now<=9000;now+=2000)uart_reply(&c,&f,&s,sample,now-100,now,8.7f,true,out);
    assert(c.coast_reduction_sent);
    const float pilots[]={15.183f,14.1247f,12.916f,12.173f,10.8107f,10.0031f,9.2915f};
    const int64_t times[]={11000,29000,49000,55000,73000,91000,109000};
    int64_t bulk_deadline=0,near_deadline=0;
    for(unsigned i=0;i<7;i++){
        sample.pilot_a=pilots[i];uart_reply(&c,&f,&s,sample,times[i]-100,times[i],8.7f,true,out);
        if(i==0){bulk_deadline=c.coast_until;assert(c.coast_bulk_used&&!c.coast_near_used&&c.coast_active);near(out[0],32.8f);}
        else if(i<4){assert(c.coast_until==bulk_deadline&&!c.coast_active&&!c.coast_near_used);}
        else if(i==4){near_deadline=c.coast_until;assert(c.coast_near_used&&c.coast_active&&near_deadline>bulk_deadline);near(out[0],32.8f);}
        else{assert(c.coast_until==near_deadline&&!c.coast_active&&out[0]>32.8f);}
    }
    /* Undershoot may release immediately, but neither consumed budget refills
       on a rebound or a temporary START. Only a new target/phase/Stop/fault does. */
    sample.pilot_a=8.4f;uart_reply(&c,&f,&s,sample,111000,111000,8.7f,true,out);
    assert(!c.coast_active&&c.coast_bulk_used&&c.coast_near_used&&out[0]<32.8f);
    sample.pilot_a=12;uart_reply(&c,&f,&s,sample,113000,113000,8.7f,true,out);
    sample.pilot_a=11;uart_reply(&c,&f,&s,sample,115000,115000,8.7f,true,out);assert(!c.coast_active);
    sample.car_charging=false;uart_reply(&c,&f,&s,sample,117000,117000,8.7f,true,out);assert(c.coast_bulk_used&&c.coast_near_used);
    sample.car_charging=true;uart_reply(&c,&f,&s,sample,119000,119000,12,true,out);assert(!c.coast_bulk_used&&!c.coast_near_used);
}
static void coast_zone_budget_and_resets(void){
    shell_http_control_t c;manual_report_filter_t f;settings_t s;float out[3];prepare_coast(&c,&f,&s,out);
    shell_http_sample_t sample={.pilot_valid=true,.car_charging=true,.pilot_a=12.1f};
    uart_reply(&c,&f,&s,sample,10800,11000,8.7f,true,out);assert(c.coast_bulk_used&&!c.coast_near_used&&c.coast_until==19005);
    sample.pilot_a=11.4f;uart_reply(&c,&f,&s,sample,12800,13000,8.7f,true,out);
    assert(c.coast_bulk_used&&c.coast_near_used&&c.coast_until==21005); /* Reserved near hold can replace active bulk once. */
    sample.pilot_a=11;uart_reply(&c,&f,&s,sample,14800,15000,8.7f,true,out);assert(c.coast_until==21005);
    prepare_coast(&c,&f,&s,out);sample.pilot_a=11.136667f;
    uart_reply(&c,&f,&s,sample,10800,11000,8.7f,true,out);assert(!c.coast_bulk_used&&c.coast_near_used);
    sample.pilot_a=10.8f;uart_reply(&c,&f,&s,sample,20800,21000,8.7f,true,out);
    assert(!c.coast_active&&!c.coast_bulk_used&&c.coast_near_used); /* Unused bulk cannot substitute inside near zone. */
    for(unsigned action=0;action<4;action++){
        prepare_coast(&c,&f,&s,out);sample.pilot_a=11.136667f;
        uart_reply(&c,&f,&s,sample,10800,11000,8.7f,true,out);assert(c.coast_near_used);
        float target=8.7f;if(action==0)target=0;if(action==1)target=12;if(action==2)s.charge_phases=1;if(action==3)c.lease_until=13000;
        uart_reply(&c,&f,&s,sample,13000,13000,target,true,out);assert(!c.coast_bulk_used&&!c.coast_near_used);
    }
}
static void feedback_deadband_boundaries(void){
    const float offsets[]={0,.8f,2},targets[]={8.7f,12,16},errors[]={-.26f,-.24f,-.20f,-.14f,0,.14f,.20f,.24f,.26f};
    for(unsigned phases=1;phases<=3;phases+=2)for(unsigned o=0;o<3;o++)for(unsigned t=0;t<3;t++)
    for(unsigned e=0;e<sizeof(errors)/sizeof(*errors);e++){
        shell_http_control_t c;shell_http_trial_arm(&c,1000);c.normal_offset_a=offsets[o];
        float goal=fminf(16,fmaxf(8.7f,targets[t]+.25f)),out[3];
        shell_http_sample_t sample={.pilot_valid=true,.car_charging=true,.pilot_a=goal+errors[e]};
        shell_http_trial_report(&c,&sample,1000,1000,targets[t],phases,32,8.7f,16,true,out);
        bool outside=phases==3?fabsf(errors[e])>.15f:fabsf(errors[e])>.25f;
        near(out[0],32+offsets[o]+(outside?errors[e]:0));
        assert(!c.fault&&c.stage==HTTP_TRIAL_BALANCE); /* Stage's .45 band is independent. */
        for(unsigned p=1;p<3;p++)near(out[p],p<phases?out[0]:0);
    }
    /*12.25 is exact. The nearest float to12.25+/- .15 lies just inside;
      adjacent floats straddle that boundary. At+/-.25 the edge is exact. */
    for(unsigned phases=1;phases<=3;phases+=2)for(int sign=-1;sign<=1;sign+=2){
        float band=phases==3?.15f:.25f,edge=12.25f+sign*band;
        const float pilots[]={nextafterf(edge,12.25f),edge,nextafterf(edge,sign>0?INFINITY:-INFINITY)};
        for(unsigned n=0;n<3;n++){
            shell_http_control_t c;shell_http_trial_arm(&c,1000);float out[3];
            shell_http_sample_t sample={.pilot_valid=true,.car_charging=true,.pilot_a=pilots[n]};
            shell_http_trial_report(&c,&sample,1000,1000,12,phases,32,8.7f,16,true,out);
            bool neutral=n==0||(n==1&&phases==3);
            near(out[0],neutral?32.8f:32.8f+pilots[n]-12.25f);
        }
    }
    /* Minimum clamping remains8.7 even with negative bias. */
    shell_http_control_t c;shell_http_trial_arm(&c,1000);c.pilot_bias_a=-.5f;float out[3];
    shell_http_sample_t sample={.pilot_valid=true,.car_charging=true,.pilot_a=8.9f};
    shell_http_trial_report(&c,&sample,1000,1000,8.7f,3,32,8.7f,16,true,out);near(out[0],33.0f);
}
static void feedback_deadband_recorded_uart(void){
    /* Exogenous192533 pilot plateaus and reconstructed successful UART times:
       validate output timing only, not a closed-loop physical prediction. */
    const float pilots[]={9.16f,9.18f};
    const int64_t replies[2][7]={{253058,255118,258373,260449,262504,264579,266649},
        {329861,331924,333985,336054,337314,339384,341453}};
    const int64_t receipts[2][7]={{251473,254929,257186,259391,261616,263834,266039},
        {329552,331763,332992,335233,335233,337495,339719}};
    for(unsigned phases=1;phases<=3;phases+=2)for(unsigned series=0;series<2;series++){
        settings_t s=trial_settings(phases);shell_http_control_t c;manual_report_filter_t f={0};float out[3];
        int64_t seed=series==0?249000:327000;shell_http_trial_arm(&c,seed);c.lease_until=1000000;
        shell_http_sample_t sample={.pilot_valid=true,.car_charging=true,.pilot_a=series==0?8.98f:9.04f};
        uart_reply(&c,&f,&s,sample,seed-100,seed,8.7f,true,out);near(out[0],32.8f);
        sample.pilot_a=pilots[series];float previous=out[0];
        for(unsigned i=0;i<7;i++){
            uart_reply(&c,&f,&s,sample,receipts[series][i],replies[series][i],8.7f,true,out);
            if(phases==3){assert(out[0]>32.8f&&out[0]>=previous&&out[0]-previous<=.04001f);if(i==0)near(out[0],32.84f);}
            else near(out[0],32.8f);
            assert(!c.coast_active&&!c.coast_bulk_used&&!c.coast_near_used);previous=out[0];
        }
        near(out[0],phases==3?(series==0?33.01f:33.03f):32.8f);
        /* Existing release discards obsolete fine reduction immediately. */
        const float lower[]={8.75f,8.71f};
        for(unsigned i=0;i<2;i++){
            int64_t now=replies[series][6]+2000*(i+1);sample.pilot_a=lower[i];
            uart_reply(&c,&f,&s,sample,now-100,now,8.7f,true,out);
            near(out[0],phases==3?(i==0?32.60f:32.56f):32.8f);
            for(unsigned p=1;p<3;p++)near(out[p],p<phases?out[0]:0);
        }
    }
}
static void feedback_deadband_does_not_narrow_coast(void){
    const float pilots[]={9.151f,9.199f,9.201f};
    for(unsigned i=0;i<3;i++){
        shell_http_control_t c;manual_report_filter_t f;settings_t s;float out[3];prepare_coast(&c,&f,&s,out);
        shell_http_sample_t sample={.pilot_valid=true,.car_charging=true,.pilot_a=pilots[i]};
        uart_reply(&c,&f,&s,sample,10800,11000,8.7f,true,out);
        assert(c.coast_active==(i==2)); /* Still goal+.25, never the narrower feedback band. */
        if(i==2){
            int64_t deadline=c.coast_until;sample.pilot_a=8.75f;
            uart_reply(&c,&f,&s,sample,12800,13000,8.7f,true,out);near(out[0],32.8f);assert(c.coast_active&&c.coast_until==deadline);
            sample.pilot_a=8.701f;uart_reply(&c,&f,&s,sample,14800,15000,8.7f,true,out);near(out[0],32.8f);assert(c.coast_active);
            sample.pilot_a=8.699f;uart_reply(&c,&f,&s,sample,16800,17000,8.7f,true,out);
            near(out[0],32.549f);assert(!c.coast_active&&c.coast_near_used); /* Release remains goal-.25, budget not refilled. */
        }
    }
}
static void three_phase_hybrid_recorded_start(void){
    /* Failed195133 exogenous trace. This proves only proposed frame behavior,
       not that keeping headroom would have changed the vehicle's response. */
    settings_t s=trial_settings(3);shell_http_control_t c;manual_report_filter_t f={0};float out[3];
    shell_http_trial_arm(&c,20000);c.lease_until=100000;
    shell_http_sample_t sample={.pilot_valid=true};
    uart_reply(&c,&f,&s,sample,19205,20814,8.7f,true,out);near(out[0],24);
    sample.pilot_a=8;sample.car_charging=true;
    const int64_t receipts[]={38555,40749,42948},replies[]={39488,41539,43619};
    for(unsigned i=0;i<3;i++){
        uart_reply(&c,&f,&s,sample,receipts[i],replies[i],8.7f,true,out);
        for(unsigned p=0;p<3;p++){near(out[p],24);near(f.reported[p],24);}
        assert(c.stage==HTTP_TRIAL_START&&c.startup_filter_starting&&c.waiting_since==20814);
    }
    sample.car_charging=false;sample.pilot_a=0;
    uart_reply(&c,&f,&s,sample,45282,45686,8.7f,true,out);near(out[0],24);
    assert(!c.startup_charging_since&&!c.startup_charging_ready&&c.waiting_since==20814);
    uart_reply(&c,&f,&s,sample,47738,49862,0,true,out);
    for(unsigned p=0;p<3;p++)near(out[p],49);assert(!c.startup_filter_starting&&!f.active);
    /* Successful194604 begins Charging with9.25A, so its46.233 handover is
       unchanged; there is no mandatory six-second extra START boost. */
    shell_http_trial_arm(&c,20000);c.lease_until=100000;f=(manual_report_filter_t){0};
    uart_reply(&c,&f,&s,sample,20204,21389,8.7f,true,out);near(out[0],24);
    sample.car_charging=true;sample.pilot_a=9.25f;
    uart_reply(&c,&f,&s,sample,45047,46233,8.7f,true,out);near(out[0],32.8f);
    assert(c.stage!=HTTP_TRIAL_START&&!c.startup_filter_starting);
}
static void three_phase_hybrid_threshold_and_timer(void){
    settings_t s=trial_settings(3);shell_http_control_t c;manual_report_filter_t f={0};float out[3];
    shell_http_trial_arm(&c,1000);c.lease_until=100000;
    shell_http_sample_t sample={.pilot_valid=true};
    uart_reply(&c,&f,&s,sample,1000,1000,8.7f,true,out);
    sample.pilot_a=8;sample.car_charging=true;
    uart_reply(&c,&f,&s,sample,3000,3000,8.7f,true,out);near(out[0],24);
    sample.pilot_a=8.6f;
    /* A tracking report would exceed grid32 atP8.6; neither that clamp nor
       an obsolete high filter can bypass the pending START frame. */
    for(unsigned p=0;p<3;p++)f.reported[p]=32.4f;
    uart_reply(&c,&f,&s,sample,5000,5000,8.7f,true,out);
    for(unsigned p=0;p<3;p++){near(out[p],24);near(f.reported[p],24);}
    shell_http_control_t saved=c;manual_report_filter_t saved_filter=f;
    sample.pilot_a=12;
    uart_reply(&c,&f,&s,sample,6000,6000,8.7f,false,out);
    assert(!memcmp(&c,&saved,sizeof(c))&&!memcmp(&f,&saved_filter,sizeof(f)));
    sample.pilot_a=8.699f;uart_reply(&c,&f,&s,sample,7000,7000,8.7f,true,out);near(out[0],24);
    sample.pilot_a=8.7f;uart_reply(&c,&f,&s,sample,7100,7100,8.7f,true,out);near(out[0],32.55f);
    assert(!c.startup_filter_starting&&!c.fault);
    shell_http_trial_arm(&c,1000);c.lease_until=100000;f=(manual_report_filter_t){0};sample.pilot_a=0;sample.car_charging=false;
    uart_reply(&c,&f,&s,sample,1000,1000,8.7f,true,out);sample.pilot_a=8;sample.car_charging=true;
    for(int64_t at=3000;at<=9000;at+=2000){uart_reply(&c,&f,&s,sample,at,at,8.7f,true,out);near(out[0],24);}
    uart_reply(&c,&f,&s,sample,9000,11000,8.7f,true,out);near(out[0],24);assert(!c.startup_charging_ready);
    uart_reply(&c,&f,&s,sample,11001,11001,8.7f,true,out);near(out[0],31.85f);
    assert(c.startup_charging_ready&&!c.startup_filter_starting);
}
static void three_phase_hybrid_resets_and_deadline(void){
    for(unsigned reset=0;reset<9;reset++){
        settings_t s=trial_settings(3);shell_http_control_t c;manual_report_filter_t f={0};float out[3];
        shell_http_trial_arm(&c,1000);c.lease_until=100000;shell_http_sample_t sample={.pilot_valid=true};
        uart_reply(&c,&f,&s,sample,1000,1000,8.7f,true,out);sample.pilot_a=8;sample.car_charging=true;
        for(int64_t at=3000;at<=7000;at+=2000)uart_reply(&c,&f,&s,sample,at,at,8.7f,true,out);
        int64_t at=9000,now=9000;float target=8.7f;
        if(reset==0)sample.car_charging=false;
        if(reset==1)sample.pilot_a=0;
        if(reset==2)at=now=11000;
        if(reset==3)at=6000;
        if(reset==4)at=5000;
        if(reset==5)target=0;
        if(reset==6)target=12;
        if(reset==7)s.charge_phases=1;
        if(reset==8)sample.pilot_valid=false;
        uart_reply(&c,&f,&s,sample,at,now,target,true,out);assert(!c.startup_charging_ready);
        if(reset==0||reset==1)assert(c.waiting_since==1000&&!c.startup_charging_since);
        if(reset==2||reset==3){assert(c.startup_charging_since==at&&c.waiting_since==1000);near(out[0],24);}
        if(reset==4||reset==5||reset==8){for(unsigned p=0;p<3;p++)near(out[p],49);assert(!c.startup_filter_starting);}
        if(reset==6||reset==7)assert(!c.startup_filter_starting&&c.stage!=HTTP_TRIAL_START);
    }
    for(unsigned flip=0;flip<2;flip++){
        settings_t s=trial_settings(3);shell_http_control_t c;manual_report_filter_t f={0};float out[3];
        shell_http_trial_arm(&c,1000);c.lease_until=100000;shell_http_sample_t sample={.pilot_valid=true};
        uart_reply(&c,&f,&s,sample,1000,1000,8.7f,true,out);
        for(int64_t at=3000;at<=39000;at+=2000){
            sample.car_charging=flip==0||at%4000==3000;sample.pilot_a=sample.car_charging?8:0;
            /* Interrupt fresh-receipt continuity without extending START. */
            if(flip==0&&at%8000==7000)sample.car_charging=false;
            uart_reply(&c,&f,&s,sample,at,at,8.7f,true,out);assert(c.waiting_since==1000&&!c.fault);
        }
        /* A late qualifying pilot may not bypass the original40s watchdog. */
        sample.car_charging=true;sample.pilot_a=9.25f;
        uart_reply(&c,&f,&s,sample,41001,41001,8.7f,true,out);
        assert(c.fault&&c.stage==HTTP_TRIAL_FAULT&&!c.startup_filter_starting);
        for(unsigned p=0;p<3;p++)near(out[p],49);
    }
    /* A running downstep or first observation of an already charging session
       must not inject a new24-A START state solely because pilot is8A. */
    settings_t s=trial_settings(3);shell_http_control_t c;manual_report_filter_t f={0};float out[3];
    shell_http_trial_arm(&c,1000);shell_http_sample_t sample={.pilot_valid=true,.car_charging=true,.pilot_a=12.25f};
    uart_reply(&c,&f,&s,sample,1000,1000,12,true,out);sample.pilot_a=8;
    uart_reply(&c,&f,&s,sample,3000,3000,8.7f,true,out);near(out[0],32);assert(c.stage!=HTTP_TRIAL_START&&!c.startup_filter_starting); /* Existing running-filter grid clamp. */
    shell_http_trial_arm(&c,1000);f=(manual_report_filter_t){0};
    uart_reply(&c,&f,&s,sample,1000,1000,8.7f,true,out);near(out[0],31.85f);assert(c.stage!=HTTP_TRIAL_START&&!c.startup_filter_starting);
}
typedef struct { const char *rate; bool valid,single; float current; } fixture_t;
static void parser_recorded_single_phase(void){
    const fixture_t fixtures[]={
        {"Charging rate: 1.76kw Single phase [7.66A 0.95pf ] ",true,true,7.66f},
        {"Charging rate: 0kw Single phase [0A ]",true,true,0},
        {"Charging rate: 30kw Single phase [40A ]",true,true,40},
        {"Charging rate: 1.76kw 1 phase [7.66A 0.95pf ] ",true,false,7.66f},
        {"Charging rate: 5.9kw 3 phase [8.5A 0.95pf ] ",true,false,8.5f},
        {"Charging rate: 0kw 3 phase [0A ]",true,false,0},
        {"Charging rate: 30kw 1 phase [40A ]",true,false,40},
        {"Charging rate: nankw Single phase [8A ]",false,true,0},
        {"Charging rate: infkw Single phase [8A ]",false,true,0},
        {"Charging rate: -1kw Single phase [8A ]",false,true,0},
        {"Charging rate: 30.01kw Single phase [8A ]",false,true,0},
        {"Charging rate: 1kw Single phase [nanA ]",false,true,0},
        {"Charging rate: 1kw Single phase [infA ]",false,true,0},
        {"Charging rate: 1kw Single phase [-1A ]",false,true,0},
        {"Charging rate: 1kw Single phase [40.01A ]",false,true,0},
        {"Charging rate: 1kw Single phase [3A",false,true,0},
        {"Charging rate: 1kw Single phase [3 0.95pf ]",false,true,0},
        {"Charging rate: 1kw Single phase [3a ]",false,true,0},
        {"Charging rate: 1kw single phase [3A ]",false,true,0},
        {"Charging rate: 1kw Single phases [3A ]",false,true,0},
        {"Charging rate: 1kw Singlephase [3A ]",false,true,0},
        {"Charging rate: 1kw Single-phase [3A ]",false,true,0},
        {"Charging rate: 1kw Single phase 3A ]",false,true,0},
        {"Charging rate: 1kw Double phase [3A ]",false,false,0},
        {"Charging rate: 1kW Single phase [3A ]",false,true,0},
        {"charging rate: 1kw Single phase [3A ]",false,true,0},
        {"Charging rate: nankw 3 phase [8A ]",false,false,0},
        {"Charging rate: infkw 1 phase [8A ]",false,false,0},
        {"Charging rate: -1kw 3 phase [8A ]",false,false,0},
        {"Charging rate: 30.01kw 1 phase [8A ]",false,false,0},
        {"Charging rate: 1kw 0 phase [8A ]",false,false,0},
        {"Charging rate: 1kw 2 phase [8A ]",false,false,0},
        {"Charging rate: 1kw 4 phase [8A ]",false,false,0},
        {"Charging rate: 1kw 3 phase [nanA ]",false,false,0},
        {"Charging rate: 1kw 3 phase [infA ]",false,false,0},
        {"Charging rate: 1kw 3 phase [-1A ]",false,false,0},
        {"Charging rate: 1kw 3 phase [40.01A ]",false,false,0},
        {"Charging rate: 1kw 3 phase [3A",false,false,0},
        {"Charging rate: 1kw 3 phase [3 ]",false,false,0},
        {"",false,false,0},
    };
    for(unsigned i=0;i<sizeof(fixtures)/sizeof(*fixtures);i++){
        const fixture_t *f=&fixtures[i];
        cJSON *json=cJSON_CreateObject(),*limit=cJSON_AddObjectToObject(json,"limit"),*tx=cJSON_AddObjectToObject(json,"transaction");
        assert(json&&limit&&tx);
        cJSON_AddNumberToObject(limit,"current",8.75);
        cJSON_AddStringToObject(tx,"statusCar","Charging");
        cJSON_AddStringToObject(json,"chargingRate",f->rate);
        shell_http_sample_t sample={.current_valid=true,.current_a=99};
        shell_http_sample_parse(json,&sample);
        bool expected=f->valid;
        assert(sample.current_valid==expected);
        assert(sample.pilot_valid&&sample.pilot_a==8.75f&&sample.car_charging);
        if(expected)assert(fabsf(sample.current_a-f->current)<.0001f);
        else assert(sample.current_a==0);
        cJSON_Delete(json);
    }
    /* Wrong JSON types and missing properties cannot masquerade as valid telemetry. */
    const char *json_cases[]={"{}","{\"chargingRate\":null}","{\"chargingRate\":12}","{\"chargingRate\":{}}"};
    for(unsigned i=0;i<sizeof(json_cases)/sizeof(*json_cases);i++){
        cJSON *json=cJSON_Parse(json_cases[i]);assert(json);
        shell_http_sample_t sample={.pilot_valid=true,.current_valid=true,.car_charging=true,.current_a=99};
        shell_http_sample_parse(json,&sample);
        assert(!sample.current_valid&&!sample.pilot_valid&&!sample.car_charging&&sample.current_a==0);cJSON_Delete(json);
    }
}

static void lower_floor_preserves_start_guard(void){
    const float floors[]={6.8f,7.8f,8.5f,8.7f};
    for(unsigned i=0;i<sizeof(floors)/sizeof(*floors);i++){
        shell_http_control_t c;shell_http_trial_arm(&c,1000);c.lease_until=99999;
        shell_http_sample_t s={.pilot_valid=true,.pilot_a=0};float out[3];
        shell_http_trial_report(&c,&s,1000,1000,8.7f,3,32,floors[i],16,true,out);
        c.startup_filter_starting=true;
        s.pilot_a=8;s.car_charging=true;
        shell_http_trial_report(&c,&s,2000,2000,8.7f,3,32,floors[i],16,true,out);
        assert(c.stage==HTTP_TRIAL_START&&out[0]==24);
        shell_http_trial_report(&c,&s,4000,4000,8.7f,3,32,floors[i],16,true,out);
        shell_http_trial_report(&c,&s,6000,6000,8.7f,3,32,floors[i],16,true,out);
        shell_http_trial_report(&c,&s,8000,8000,8.7f,3,32,floors[i],16,true,out);
        assert(c.stage==HTTP_TRIAL_START&&!c.startup_charging_ready);
        shell_http_trial_report(&c,&s,9000,9000,8.7f,3,32,floors[i],16,true,out);
        assert(c.stage!=HTTP_TRIAL_START&&c.startup_charging_ready&&!c.fault);
    }
}
static void lower_floor_tapers_reduction(void){
    const float pilots[]={8.3f,9.0f,10.2f,12.0f,16.0f};
    for(unsigned i=0;i<sizeof(pilots)/sizeof(*pilots);i++){
        shell_http_control_t c;shell_http_trial_arm(&c,1000);
        shell_http_sample_t s={.pilot_valid=true,.car_charging=true,.pilot_a=pilots[i]};float out[3];
        shell_http_trial_report(&c,&s,1000,1000,7.8f,3,32,7.8f,16,true,out);
        float expected=32.8f+fminf(1.2f,.7f*(pilots[i]-8.05f));
        near(out[0],expected);assert(!c.fault&&out[0]<=34.0f);
        shell_http_control_t original=c;s.current_a=30;
        shell_http_trial_report(&c,&s,1000,1000,7.8f,3,32,7.8f,16,false,out);
        assert(!memcmp(&c,&original,sizeof(c)));near(out[0],expected);
    }
    shell_http_control_t c;shell_http_trial_arm(&c,1000);
    shell_http_sample_t s={.pilot_valid=true,.car_charging=true,.pilot_a=7.5};float out[3];
    shell_http_trial_report(&c,&s,1000,1000,7.8f,3,32,7.8f,16,true,out);
    near(out[0],32.25f); /* An undershoot still gets the full upward correction. */
    settings_t settings;settings_defaults(&settings);settings.mode=MODE_MANUAL;
    settings.charge_phases=3;settings.grid_limit_a=32;settings.current_offset_a=.8f;settings.min_charge_a=7.8f;
    /* The rejected candidate always selected the slow ramp, even far away.
       Bulk descent must now enter the existing fast ramp, with the same
       small near-goal steps and no change to its 0.04-A frame bound. */
    manual_report_filter_t filter={.active=true,.last_ms=1000,.reported={32.8f,32.8f,32.8f}};
    float bulk[3]={34,34,34};manual_report_smooth(&filter,&settings,7.8f,2000,bulk);
    near(bulk[0],33.1f);
    filter=(manual_report_filter_t){.active=true,.last_ms=1000,.reported={32.8f,32.8f,32.8f}};
    float close[3]={33.1f,33.1f,33.1f};manual_report_smooth(&filter,&settings,7.8f,2000,close);
    near(close[0],32.82f);
}
static void lower_floor_medium_descent_speed(void){
    settings_t settings;settings_defaults(&settings);settings.mode=MODE_MANUAL;
    settings.charge_phases=3;settings.grid_limit_a=32;settings.current_offset_a=.8f;settings.min_charge_a=7.8f;
    shell_http_control_t c;shell_http_trial_arm(&c,1000);c.stage=HTTP_TRIAL_WAIT;
    manual_report_filter_t seed={.active=true,.last_ms=1000,.reported={32.8f,32.8f,32.8f}};
    manual_report_filter_t filter=seed;float out[3]={33.4f,33.4f,33.4f};
    shell_http_trial_smooth(&c,&filter,&settings,7.8f,2000,true,out);
    for(unsigned p=0;p<3;p++)near(out[p],32.88f);
    filter=seed;for(unsigned p=0;p<3;p++)out[p]=33.4f;
    shell_http_control_t original=c;
    shell_http_trial_smooth(&c,&filter,&settings,7.8f,3000,false,out);
    near(out[0],32.96f);assert(!memcmp(&filter,&seed,sizeof(filter))&&!memcmp(&c,&original,sizeof(c)));
    filter=seed;for(unsigned p=0;p<3;p++)out[p]=33.1f;
    shell_http_trial_smooth(&c,&filter,&settings,7.8f,2000,true,out);near(out[0],32.82f);
    filter=seed;for(unsigned p=0;p<3;p++){out[p]=33.4f;filter.reported[p]=32.2f;}
    shell_http_trial_smooth(&c,&filter,&settings,7.8f,2000,true,out);near(out[0],32.88f);
    /* The faster band cannot delay releasing a queued reduction. */
    filter=seed;for(unsigned p=0;p<3;p++){out[p]=32.8f;filter.reported[p]=33.4f;}
    shell_http_trial_smooth(&c,&filter,&settings,7.8f,2000,true,out);near(out[0],32.8f);
    for(unsigned p=0;p<3;p++)out[p]=49;
    shell_http_trial_smooth(&c,&filter,&settings,7.8f,3000,true,out);near(out[0],49);assert(!filter.active);
    /* Existing 6-kW and one-phase smoothing remain identical. */
    for(unsigned mode=0;mode<2;mode++){
        settings.charge_phases=mode?1:3;
        float target=mode?7.8f:8.7f;
        filter=seed;manual_report_filter_t expected=seed;
        float normal[3]={33.4f,33.4f,33.4f};for(unsigned p=0;p<3;p++)out[p]=normal[p];
        manual_report_smooth(&expected,&settings,target,2000,normal);
        shell_http_trial_smooth(&c,&filter,&settings,target,2000,true,out);
        for(unsigned p=0;p<3;p++)near(out[p],normal[p]);
    }
}
int main(void){
    parser();parser_recorded_single_phase();gates();consistent_goal();neutral_compensation();start_confirmation();
    startup_handover();startup_scope_and_boundaries();startup_cancel_and_stop();
    start_permission_and_car_watchdog();coast_observed_uart_trajectory();coast_restart_recovery_and_delivery();coast_scope_resets_and_faults();
    one_phase_recorded_handover();one_phase_guard_boundaries();coast_bounded_recorded_descent();coast_zone_budget_and_resets();
    feedback_deadband_boundaries();feedback_deadband_recorded_uart();feedback_deadband_does_not_narrow_coast();
    three_phase_hybrid_recorded_start();three_phase_hybrid_threshold_and_timer();three_phase_hybrid_resets_and_deadline();
    lower_floor_preserves_start_guard();
    lower_floor_tapers_reduction();
    lower_floor_medium_descent_speed();
    puts("PASS: private3p initialized-START hybrid with explicit24 pending and absolute40s deadline; trace-only UART replay, unchanged1p/normal paths, .15 feedback, bounded coast,Stop/fault/copy/reset guards");
}
