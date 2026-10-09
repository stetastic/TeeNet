#pragma once
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#define EMS_DAYS 30
#define EMS_HISTORY 120
#define EMS_SOURCES 2
#define EMS_METER_TTL 10000 /* Shelly 3EM / Modbus-TCP meters need a little more time to answer reliably */
#define EMS_INPUT_TTL 30000
#define EMS_HOUSE_TTL 10000
#define EMS_GRID_FALLBACK_W 8000.0f
#define EMS_CHARGE_STOP_CONFIRM_MS 20000
#define EMS_CHARGE_STOP_LIMIT 5
#define EMS_CHARGE_STOP_WINDOW_MS 300000
#define EMS_SETTINGS_VERSION 26
#define EMS_MIN_CHARGE_A 6.0f
#define EMS_BATTERY_CAPACITY_KWH 20.0f
#define EMS_BATTERY_DISCHARGE_MAX_W 4000.0f
#define EMS_BATTERY_BUFFER_MS 900000
#define EMS_BATTERY_BUFFER_RECOVERY_MS 60000
