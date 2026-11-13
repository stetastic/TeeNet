#pragma once
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
/* Owned exclusively by meter_task. Configuration handlers never touch its FD. */
typedef struct {
    int sock;
    char host[64];
    uint32_t revision;
    uint16_t transaction;
    uint8_t unit;
    bool transaction_seeded;
} wallbox_modbus_session_t;
static inline void wallbox_modbus_session_close(wallbox_modbus_session_t *session);
static inline bool wallbox_modbus_session_read(wallbox_modbus_session_t *session,const char *host,uint8_t unit,uint32_t revision,uint16_t start,uint16_t count,uint8_t *data,size_t capacity,int timeout_ms);
