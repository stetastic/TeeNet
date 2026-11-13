#include "shelly_modbus.h"

#include <math.h>
#include <string.h>

/* Shelly stores IEEE-754 values as two big-endian Modbus registers with the
   low word first (CDAB / "mixed" word order). */
static float mixed_float(const uint8_t *data) {
    uint32_t bits=((uint32_t)data[2]<<24)|((uint32_t)data[3]<<16)|
                  ((uint32_t)data[0]<<8)|data[1];
    float value;
    memcpy(&value,&bits,sizeof(value));
    return value;
}

static bool current_ok(float value) {
    return isfinite(value) && value>=0 && value<=200;
}

static bool power_ok(float value) {
    return isfinite(value) && value>=-1000000 && value<=1000000;
}

bool shelly_modbus_decode_voltage(const uint8_t *data,size_t length,float voltage[3]){
    if(!data||!voltage||length<SHELLY_EM_COUNT*2)return false;
    float values[3];
    for(unsigned i=0;i<3;i++){
        values[i]=mixed_float(data+(1020+20*i-SHELLY_EM_START)*2);
        if(!isfinite(values[i])||values[i]<0||values[i]>300)return false;
    }
    memcpy(voltage,values,sizeof(values));return true;
}

bool shelly_modbus_decode_em(const uint8_t *data,size_t length,
                             shelly_modbus_reading_t *out) {
    if(!data || !out || length<SHELLY_EM_COUNT*2) return false;
    /* Buffer begins at protocol address 1011. */
    const size_t power=(1013-SHELLY_EM_START)*2;
    const size_t current_a=(1022-SHELLY_EM_START)*2;
    const size_t current_b=(1042-SHELLY_EM_START)*2;
    const size_t current_c=(1062-SHELLY_EM_START)*2;
    shelly_modbus_reading_t next={
        .current_a={mixed_float(data+current_a),mixed_float(data+current_b),mixed_float(data+current_c)},
        .active_power_w=mixed_float(data+power),.phases=3
    };
    if(!current_ok(next.current_a[0]) || !current_ok(next.current_a[1]) ||
       !current_ok(next.current_a[2]) || !power_ok(next.active_power_w)) return false;
    next.voltage_valid=shelly_modbus_decode_voltage(data,length,next.voltage_v);
    *out=next;
    return true;
}

bool shelly_modbus_decode_em1(const uint8_t *data,size_t length,
                              shelly_modbus_reading_t *out) {
    if(!data || !out || length<SHELLY_EM1_COUNT*2) return false;
    /* Buffer begins at protocol address 2003: voltage, current, power. */
    shelly_modbus_reading_t next={
        .current_a={mixed_float(data+4),0,0},
        .active_power_w=mixed_float(data+8),.phases=1
    };
    if(!current_ok(next.current_a[0]) || !power_ok(next.active_power_w)) return false;
    *out=next;
    return true;
}
