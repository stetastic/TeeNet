#include "phase_voltage.h"
static phase_voltage_proof_t phase_voltage;
static inline bool phase_voltage_shared_locked(void){
    return phase_uses_voltage(&settings)&&settings.phase_voltage_protocol==0&&
        !strcmp(settings.wallbox_meter_type,"shelly_gen2")&&!strcmp(settings.phase_voltage_host,settings.wallbox_meter_host);
}
static unsigned phase_voltage_observed_locked(int64_t now){
    return wifi_online?phase_voltage_observed(&phase_voltage,now):0;
}
