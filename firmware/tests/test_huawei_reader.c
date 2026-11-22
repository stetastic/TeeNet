#include "ems_core.h"
#include "huawei_modbus.h"
#include <assert.h>
#include <stdio.h>
#include <string.h>

static settings_t settings;
static bool wifi_online=true, restarting, ota_in_progress;
static int64_t actual_at, house_power_at, house_current_at, pv_at, pv_generation_at;
static int64_t battery_soc_at, battery_charge_at, battery_discharge_at;
static float house_power_w, house_a[3], pv_generation_w, battery_soc, battery_charge_w, battery_discharge_w;
static bool locked, fail_start;
static unsigned attempts, deleted, sockets;
static void (*worker)(void *);
typedef void *SemaphoreHandle_t;
#define LOCK() do { assert(!locked); locked=true; } while(0)
#define UNLOCK() do { assert(locked); locked=false; } while(0)
#define pdPASS 1
#define pdTRUE 1
#define SHUT_RDWR 2
static int64_t now_ms(void){return 1000;}
static unsigned ticks(int64_t ms){return (unsigned)ms;}
static void vTaskDelay(unsigned ms){assert(!locked&&ms);settings.huawei_enabled=false;}
static void vTaskDelete(void *self){assert(!locked&&!self);deleted++;}
static int xTaskCreate(void (*entry)(void *),const char *name,unsigned bytes,void *arg,unsigned priority,void *handle){
    assert(!locked&&!arg&&!handle&&priority==6&&bytes==4096&&!strcmp(name,"huawei"));
    attempts++;
    if(fail_start)return 0;
    worker=entry;
    return pdPASS;
}
static int xSemaphoreTake(SemaphoreHandle_t s,unsigned wait){assert(s&&wait&&!locked);return pdTRUE;}
static void xSemaphoreGive(SemaphoreHandle_t s){assert(s&&!locked);}
static unsigned esp_random(void){return 1;}
static int tcp_connect_timeout(const char *host,int port,int wait){
    assert(!locked&&host[0]&&port==502&&wait);sockets++;return -1;
}
static bool socket_exact(int s,uint8_t *p,size_t n,bool write,int64_t deadline){
    (void)s;(void)p;(void)n;(void)write;(void)deadline;assert(0);return false;
}
static int shutdown(int s,int how){(void)s;(void)how;assert(0);return 0;}
static int close(int s){(void)s;assert(0);return 0;}
static float pv_available_current_locked(int64_t now,float watts){(void)now;(void)watts;assert(locked);return 0;}

#include "huawei_network.inc"

int main(void){
    settings_defaults(&settings);
    huawei_io=&settings;
    huawei_reader_ensure();
    assert(!attempts&&!huawei_reader_running);
    char model[31];
    assert(!huawei_probe("discovery.local",1,model)&&sockets==1);

    settings.huawei_enabled=settings.huawei_pv=settings.pv_display_enabled=true;
    strcpy(settings.huawei_host,"configured-hostname-longer-than-an-ipv4.local");
    ota_in_progress=true;huawei_reader_ensure();ota_in_progress=false;
    restarting=true;huawei_reader_ensure();restarting=false;
    assert(!attempts);
    fail_start=true;huawei_reader_ensure();
    assert(attempts==1&&!huawei_reader_running);
    fail_start=false;
    for(unsigned cycle=0;cycle<1000;cycle++){
        settings.huawei_enabled=true;
        huawei_reader_ensure();huawei_reader_ensure();
        assert(worker&&huawei_reader_running&&attempts==cycle+2);
        if(cycle%2==0)settings.huawei_enabled=false; /* disabled before the worker starts */
        worker(NULL);
        assert(deleted==cycle+1&&!huawei_reader_running&&!locked);
        assert(sockets==1+(cycle+1)/2);
    }
    puts("PASS: Huawei discovery when disabled; live startup, retry, duplicate suppression and release across 1000 cycles");
}
