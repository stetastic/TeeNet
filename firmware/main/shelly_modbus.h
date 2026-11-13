#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

/* Shelly documents these as 3xxxx input registers. Modbus function 04 uses
   the protocol addresses below, i.e. the documented address minus 30000. */
#define SHELLY_EM_START 1011
#define SHELLY_EM_COUNT 55
#define SHELLY_EM1_START 2003
#define SHELLY_EM1_COUNT 6

typedef struct {
    float current_a[3];
    float active_power_w;
    unsigned phases;
    float voltage_v[3]; /* Shelly A/B/C order. */
    bool voltage_valid;
} shelly_modbus_reading_t;
bool shelly_modbus_decode_voltage(const uint8_t *data,size_t length,float voltage[3]);

bool shelly_modbus_decode_em(const uint8_t *data, size_t length,
                             shelly_modbus_reading_t *out);
bool shelly_modbus_decode_em1(const uint8_t *data, size_t length,
                              shelly_modbus_reading_t *out);

