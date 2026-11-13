#pragma once
#include <stdbool.h>
#include <stdint.h>

/* A checkpoint is saved together with the energy totals, never independently. */
typedef struct {
    uint32_t version;
    uint64_t host_key,session_key;
    double raw_wh;
    int64_t epoch_ms;
    bool anchored,explicit_id,idle_seen;
} charge_counter_checkpoint_t;
typedef struct {
    charge_counter_checkpoint_t saved;
    int64_t sample_ms;
    bool restored,gap,ready,enabled;
    unsigned resets,rejected;
} charge_counter_t;
typedef struct {
    bool fresh,present,idle;
    const char *host,*identity,*card;
    bool energy_valid;
    double raw_wh;
    int64_t sample_ms,now_ms,epoch_ms;
} charge_counter_input_t;
typedef struct {
    double wh;
    int64_t from_ms,to_ms,from_epoch_ms,to_epoch_ms;
    bool new_session,gap;
} charge_counter_delta_t;
bool charge_counter_checkpoint_valid(const charge_counter_checkpoint_t *saved);
charge_counter_delta_t charge_counter_step(charge_counter_t *counter,bool enabled,const charge_counter_input_t *input);
