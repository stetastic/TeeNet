#pragma once
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

/* Shelly Pro 1 + ABB E290-32-20/230 + E299-11. Switch:0 sends a
   bounded toggle pulse; Input:0 is the NO and Input:1 the NC contact. */
#define SHELLY_PHASE_TTL_MS 5000
#define SHELLY_PHASE_PULSE_MS 500
typedef struct {
    char id[64],model[48],name[64];
    unsigned generation;
    bool protected_device,pro1;
    uint8_t profile,inputs; /* known 230-V profile 0..12; 255 unsupported */
} shelly_phase_identity_t;
typedef struct {
    bool output,no_closed,nc_closed;
    unsigned phases; /* 0: unknown/contradictory, 1: both mains open, 3: closed */
    uint32_t uptime;
    bool has_uptime;
} shelly_phase_feedback_t;
bool shelly_phase_identity(const char *json,shelly_phase_identity_t *out);
unsigned shelly_phase_profile_inputs(unsigned profile);
bool shelly_phase_feedback(const char *json,shelly_phase_feedback_t *out);
bool shelly_phase_safe_config(const char *json);
bool shelly_phase_feedback_for(const char *json,shelly_phase_feedback_t *out,unsigned count,bool closed_is_single);
bool shelly_phase_config_for(const char *json,unsigned count,bool pulse);
bool shelly_phase_endpoint_valid(const char *text,size_t capacity);
bool shelly_phase_pulse_path(char *out,size_t capacity);
bool shelly_phase_pulse_ack(const char *json);
/* Absolute switch control without mechanical contacts; never toggle or pulse. */
bool shelly_output_config(const char *json,unsigned id);
bool shelly_output_status(const char *json,unsigned id,bool *on);
bool shelly_output_ack(const char *json);
bool shelly_output_path(char *out,size_t capacity,const char *method,unsigned id,bool on);
void shelly_output_coil_request(uint8_t out[12],uint16_t transaction,unsigned id,bool writing,bool on);
bool shelly_output_coil_reply(const uint8_t *reply,size_t size,const uint8_t request[12],bool *on);
