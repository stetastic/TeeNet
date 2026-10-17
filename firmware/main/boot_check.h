#pragma once
#include <stdbool.h>
#include <stdint.h>
#define BOOT_CHECK_TIMEOUT_MS 90000u
#define BOOT_CHECK_STABLE_MS 5000u
#define RUNTIME_CHECK_STABLE_MS 5000u
enum {
    RUNTIME_TASK_STATUS=1u<<0,
    RUNTIME_TASK_WALLBOX=1u<<1,
    RUNTIME_TASK_METER=1u<<2,
    RUNTIME_TASK_HOUSE=1u<<3,
    RUNTIME_TASK_PHASE=1u<<4
};
typedef struct {uint32_t started,healthy_since;bool healthy;} boot_check_t;
typedef struct {uint32_t candidate,since;} runtime_check_t;
enum {BOOT_CHECK_WAIT,BOOT_CHECK_PASS,BOOT_CHECK_FAIL};
static inline int boot_check_step(boot_check_t *check,uint32_t now,bool healthy){
    if(now-check->started>=BOOT_CHECK_TIMEOUT_MS)return BOOT_CHECK_FAIL;
    if(!healthy){check->healthy=false;return BOOT_CHECK_WAIT;}
    if(!check->healthy){check->healthy=true;check->healthy_since=now;}
    return now-check->healthy_since>=BOOT_CHECK_STABLE_MS?BOOT_CHECK_PASS:BOOT_CHECK_WAIT;
}
static inline uint32_t runtime_stale_tasks(const volatile uint32_t seen[5],uint32_t required,uint32_t now){
    static const uint32_t maximum_age[5]={10000u,10000u,90000u,90000u,15000u};
    uint32_t stale=0;
    for(unsigned i=0;i<5;i++)if((required&(1u<<i))&&(!seen[i]||now-seen[i]>=maximum_age[i]))stale|=1u<<i;
    return stale;
}
static inline uint32_t runtime_check_step(runtime_check_t *check,uint32_t now,uint32_t stale){
    if(!stale){check->candidate=0;check->since=0;return 0;}
    if(check->candidate!=stale){check->candidate=stale;check->since=now;return 0;}
    return now-check->since>=RUNTIME_CHECK_STABLE_MS?stale:0;
}
