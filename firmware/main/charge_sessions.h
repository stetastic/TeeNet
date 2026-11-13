#pragma once
#include <stdbool.h>
#include <stdint.h>
#include <stddef.h>

#define CHARGE_SESSION_CAPACITY 512
#define SESSION_PAUSE_MS 300000
#define SESSION_MIN_WH 1000.0
#define SESSION_INTERRUPTED 1u
#define SESSION_METER_GAP 2u
#define SESSION_CLOCK_GAP 4u
#define SESSION_COST_SPLIT_ESTIMATED 8u

typedef struct {
    uint32_t id,start_epoch,end_epoch,start_date,charging_ms;
    float grid_wh,solar_wh,unknown_wh,cost_eur;
    uint32_t flags;
    float grid_cost_eur,solar_cost_eur;
} charge_session_t;
typedef struct {
    uint32_t version,count,next_slot,next_id,active_slot;
    charge_session_t records[CHARGE_SESSION_CAPACITY];
} session_store_t;
typedef struct {
    session_store_t store;
    int64_t last_ms,last_epoch,charge_expires,grid_expires,idle_since,start_ms;
    double previous_w,previous_grid,grid_wh,solar_wh,unknown_wh,cost,grid_cost,solar_cost;
    float grid_price,solar_price;
    bool initialized,checkpoint;
    bool metered;
    double pending_grid,pending_solar,pending_grid_cost,pending_solar_cost;
    charge_session_t pending_session;
    bool pending_active;
} sessions_t;

void sessions_init(sessions_t *s);
bool sessions_restore(sessions_t *s,const void *blob,size_t size,float legacy_grid_price,float legacy_solar_price);
void sessions_step(sessions_t *s,int64_t now,int64_t epoch,uint32_t date,
    double charge_w,int64_t charge_expires,double grid_w,int64_t grid_expires,
    float grid_price,float solar_price);
const charge_session_t *sessions_recent(const session_store_t *s,unsigned index);
bool session_matches(const charge_session_t *s,uint32_t period);
bool session_period_parse(const char *text,uint32_t *period);
bool session_recorded(const charge_session_t *row);
void sessions_meter_mode(sessions_t *s,bool enabled,bool new_session);
void sessions_add_metered(sessions_t *s,int64_t now,int64_t epoch,uint32_t date,double wh,bool gap);
