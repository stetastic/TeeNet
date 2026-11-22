#pragma once
#include "charge_sessions.h"
#include <inttypes.h>
#include <stdio.h>

/* The device key is generated locally from the station MAC (ASCII hex). */
static inline int session_archive_row(char *line,size_t size,const char *device,
                                      const charge_session_t *r,bool influx,bool first){
    double grid=(r->grid_wh+(double)r->unknown_wh)/1000,solar=r->solar_wh/1000.;
    if(influx){
        return snprintf(line,size,"teenet_session,device=%s,session=%" PRIu32 " id=%" PRIu32 "i,end_epoch=%" PRIu32
            "i,charging_s=%" PRIu32 "i,energy_kwh=%.6f,grid_kwh=%.6f,solar_kwh=%.6f,"
            "cost_eur=%.6f,grid_cost_eur=%.6f,solar_cost_eur=%.6f,flags=%" PRIu32 "i %" PRIu32 "000000000\n",
            device,r->id,r->id,r->end_epoch,r->charging_ms/1000,grid+solar,grid,solar,r->cost_eur,
            r->grid_cost_eur,r->solar_cost_eur,r->flags,r->start_epoch);
    }
    return snprintf(line,size,"%s{\"id\":%" PRIu32 ",\"date\":%" PRIu32 ",\"start_epoch\":%" PRIu32
        ",\"end_epoch\":%" PRIu32 ",\"charging_s\":%" PRIu32 ",\"energy_kwh\":%.6f,\"grid_kwh\":%.6f,"
        "\"solar_kwh\":%.6f,\"cost_eur\":%.6f,\"grid_cost_eur\":%.6f,\"solar_cost_eur\":%.6f,\"flags\":%" PRIu32 "}",
        first?"":",",r->id,r->start_date,r->start_epoch,r->end_epoch,r->charging_ms/1000,
        grid+solar,grid,solar,r->cost_eur,r->grid_cost_eur,r->solar_cost_eur,r->flags);
}
