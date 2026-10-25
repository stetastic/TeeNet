#include "ems_core.h"
#include <assert.h>
#include <math.h>
#include <stdio.h>
static bool restarting,ota_in_progress,backup_in_progress,evcc_configuring,boot_validation_pending,phase_test_active;
static bool charging,quiescent,has_measurements,meter_fresh;
static float actual[3];
static config_gate_t phase_config_gate_locked(int64_t now){(void)now;return hardware_config_gate(charging,quiescent,has_measurements,meter_fresh,actual);}
#include "ota_gate_actual.inc"
static void fixture(void){
    restarting=ota_in_progress=backup_in_progress=evcc_configuring=boot_validation_pending=phase_test_active=charging=false;
    quiescent=has_measurements=meter_fresh=true;
    for(unsigned p=0;p<3;p++)actual[p]=.1f;
}
int main(void){
    fixture();assert(ota_install_allowed_locked(1000));
    bool *flags[]={&restarting,&ota_in_progress,&backup_in_progress,&evcc_configuring,&boot_validation_pending,&phase_test_active,&charging};
    for(unsigned i=0;i<sizeof(flags)/sizeof(flags[0]);i++){fixture();*flags[i]=true;assert(!ota_install_allowed_locked(1000));}
    fixture();quiescent=false;assert(!ota_install_allowed_locked(1000));
    fixture();meter_fresh=false;assert(!ota_install_allowed_locked(1000));
    for(unsigned p=0;p<3;p++){
        const float bad[]={1,16,-1,NAN,INFINITY};
        for(unsigned i=0;i<sizeof(bad)/sizeof(bad[0]);i++){fixture();actual[p]=bad[i];assert(!ota_install_allowed_locked(1000));}
    }
    fixture();has_measurements=false;meter_fresh=false;assert(ota_install_allowed_locked(1000));
    puts("PASS: production OTA guard rejects charging, moving contactor/test, maintenance, stale/invalid/high current; uncommissioned recovery remains possible");
}
