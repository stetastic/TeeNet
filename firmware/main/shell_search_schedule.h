#pragma once
#include <stdint.h>
/* Repeated absent-device searches must not load the WLAN every five minutes. */
static inline int64_t shell_search_pause(unsigned attempts){
    return attempts==0?300000:attempts==1?900000:1800000;
}
