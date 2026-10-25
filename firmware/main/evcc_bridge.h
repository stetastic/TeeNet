#pragma once
#include <stdbool.h>
#include <stdint.h>
#include "expert_params.h"

#define EVCC_SHELL_TTL_MS (ems_param_ms(EP_EVCC_STATUS_TTL))
#define EVCC_LEASE_MS (ems_param_ms(EP_EVCC_LEASE))

typedef struct {
    /* A disconnected, B connected, C charging, D finished, F fault, ? unknown. */
    char state;
    bool evcc_usable; /* Only states already verified for evcc may release its readiness check. */
    char raw[32], reason[40];
} evcc_shell_t;

typedef struct {
    bool configured, enabled, current_set, inhibited, expired;
    bool phase_set, preparing;
    uint8_t phases;
    float current_a;
    int64_t last_contact;
} evcc_control_t;

bool evcc_shell_parse(const char *body, evcc_shell_t *out);
bool evcc_shell_fresh(const evcc_shell_t *sample, int64_t at, int64_t now);
bool shell_vehicle_fresh(const evcc_shell_t *sample, int64_t at, int64_t now);
void evcc_configure(evcc_control_t *control, bool configured);
void evcc_touch(evcc_control_t *control, int64_t now);
bool evcc_set_current(evcc_control_t *control, float value, float minimum, float maximum, int64_t now);
bool evcc_set_enabled(evcc_control_t *control, bool enabled, bool ready, int64_t now);
bool evcc_set_phases(evcc_control_t *control, unsigned phases, bool supported, bool ready, int64_t now);
unsigned evcc_desired_phases(const evcc_control_t *control, unsigned active);
void evcc_local_stop(evcc_control_t *control);
bool evcc_local_release(evcc_control_t *control, int64_t now);
bool evcc_tick(evcc_control_t *control, int64_t now);
bool evcc_emergency_stop_json(const char *body);
bool evcc_host_valid(const char *host);
