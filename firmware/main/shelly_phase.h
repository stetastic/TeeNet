#pragma once
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

/* Network switch status and optional downstream voltage proof; no auxiliary inputs. */
#define SHELLY_PHASE_TTL_MS 5000
typedef struct {
    char id[64],model[48],name[64];
    unsigned generation;
    bool protected_device,pro1;
    uint8_t profile,inputs; /* known 230-V profile 0..12; 255 unsupported */
} shelly_phase_identity_t;
typedef struct {
    bool output;
    uint32_t uptime;
    bool has_uptime;
} shelly_phase_feedback_t;
bool shelly_phase_identity(const char *json,shelly_phase_identity_t *out);
unsigned shelly_phase_profile_inputs(unsigned profile);
bool shelly_phase_feedback(const char *json,shelly_phase_feedback_t *out);
bool shelly_phase_config(const char *json);
bool shelly_phase_endpoint_valid(const char *text,size_t capacity);
bool shelly_phase_voltage_json(const char *json,bool legacy,float logical[3]);
/* Absolute switch control without mechanical contacts; never toggle or pulse. */
bool shelly_output_config(const char *json,unsigned id);
bool shelly_output_status(const char *json,unsigned id,bool *on);
bool shelly_output_ack(const char *json);
bool shelly_output_path(char *out,size_t capacity,const char *method,unsigned id,bool on);
void shelly_output_coil_request(uint8_t out[12],uint16_t transaction,unsigned id,bool writing,bool on);
bool shelly_output_coil_reply(const uint8_t *reply,size_t size,const uint8_t request[12],bool *on);
