#include "ems_core.h"
#include "expert_params.h"
#include <assert.h>
#include <math.h>
#include <stdio.h>
#include <string.h>
int main(void){
    settings_t settings;settings_defaults(&settings);
    expert_values_t values;expert_defaults(&values);
    assert(fabsf(settings.min_charge_a-7.8f)<.0001f);
    assert(fabsf(values.values[EP_MIN_CURRENT]-settings.min_charge_a)<.0001f);
    assert(settings.min_charge_a-settings.current_offset_a>=EMS_MIN_CHARGE_A);
    assert(expert_valid(&values)&&settings_valid(&settings));
    assert(fabsf(manual_minimum_power_kw(&settings,3)-5.6f)<.0001f);
    settings.nominal_v=245;
    assert(fabsf(manual_minimum_power_kw(&settings,3)-5.8f)<.0001f);
    settings.min_charge_a=10;
    assert(fabsf(manual_minimum_power_kw(&settings,3)-7.4f)<.0001f);
    assert(isnan(manual_minimum_power_kw(&settings,2)));
    /* A saved commissioned expert value survives the changed factory floor. */
    values.values[EP_MIN_CURRENT]=8.7f;
    expert_values_t decoded;assert(expert_decode(&values,sizeof(values),&decoded));
    assert(decoded.values[EP_MIN_CURRENT]==values.values[EP_MIN_CURRENT]);
    puts("PASS: stable factory floor, core/expert consistency, offset bounds, saved expert values preserved");
}
