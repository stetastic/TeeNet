#pragma once
#include <stdbool.h>
#include <stdint.h>
#define BOOT_CHECK_TIMEOUT_MS 90000u
#define BOOT_CHECK_STABLE_MS 5000u
typedef struct {uint32_t started,healthy_since;bool healthy;} boot_check_t;
enum {BOOT_CHECK_WAIT,BOOT_CHECK_PASS,BOOT_CHECK_FAIL};
static inline int boot_check_step(boot_check_t *check,uint32_t now,bool healthy){
    if(now-check->started>=BOOT_CHECK_TIMEOUT_MS)return BOOT_CHECK_FAIL;
    if(!healthy){check->healthy=false;return BOOT_CHECK_WAIT;}
    if(!check->healthy){check->healthy=true;check->healthy_since=now;}
    return now-check->healthy_since>=BOOT_CHECK_STABLE_MS?BOOT_CHECK_PASS:BOOT_CHECK_WAIT;
}
