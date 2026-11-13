#pragma once
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

/* Profiles: SH hybrid, SG PV-only, SG with supported meter registers, iHM.
   Modbus addresses below are zero based; words in 32-bit values are low first. */
enum { SUNGROW_SH, SUNGROW_SG, SUNGROW_SG_METER, SUNGROW_IHM };
typedef struct {
    bool grid_ok,pv_ok,battery_ok;
    float grid_w,pv_w,soc,charge_w,discharge_w;
} sungrow_reading_t;
bool sungrow_reply_header(const uint8_t header[9],uint16_t transaction,uint8_t unit,
    uint16_t count,uint8_t *exception);
bool sungrow_power_decode(const uint8_t *data,size_t length,bool signed_value,float scale,float *watts);
bool sungrow_sh_decode(const uint8_t *data,size_t length,sungrow_reading_t *reading);
bool sungrow_ihm_decode(const uint8_t *data,size_t length,sungrow_reading_t *reading);
