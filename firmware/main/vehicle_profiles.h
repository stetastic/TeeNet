#pragma once
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include "cJSON.h"
#define VEHICLE_COUNT 4
#define VEHICLE_CARD_BYTES 49
#define VEHICLE_PHOTO_BYTES 1536
typedef struct {
    char name[49], plate[25], cards[2][VEHICLE_CARD_BYTES];
    float capacity_kwh, maximum_kw, efficiency, target_soc;
    uint8_t soc_source; /* 0 manual, 1 vehicle-specific MQTT, 2 existing vehicle source. */
    uint8_t picture; /* 0 own image/default, 1 ID.4, 2 Tiguan, 3 GLB, 4 Born, 5 Enyaq. Fits existing padding. */
} vehicle_profile_t;
typedef struct {uint32_t version;bool enabled;vehicle_profile_t rows[VEHICLE_COUNT];} vehicle_store_t;
typedef struct {
    bool present, energy_valid;
    char card[VEHICLE_CARD_BYTES], identity[65];
    float consumed_wh;
} vehicle_session_t;
typedef struct {
    uint32_t version;bool active;uint8_t slot;
    bool measured_soc;float start_soc,target_soc,capacity_kwh,efficiency,maximum_kw,last_soc,anchor_wh;
    char card[VEHICLE_CARD_BYTES],identity[65];
} vehicle_plan_t;
bool vehicle_card(const char *text,char out[VEHICLE_CARD_BYTES]);
bool vehicle_profile_valid(const vehicle_profile_t *p);
bool vehicle_store_valid(const vehicle_store_t *s);
int vehicle_match(const vehicle_store_t *s,const char *card);
bool vehicle_profile_parse(const cJSON *o,vehicle_profile_t *p);
void vehicle_session_parse(const cJSON *transaction,vehicle_session_t *out);
bool vehicle_need(const vehicle_profile_t *p,float current_soc,float target_soc,float *grid_kwh);
double vehicle_seconds(float remaining_kwh,float power_kw);
int64_t vehicle_grid_start(int64_t deadline,float remaining_kwh,float power_kw);
bool vehicle_estimated_soc(float start,float capacity,float efficiency,double anchor_wh,double total_wh,float *soc);
