#include <assert.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <time.h>
#include "cJSON.h"
static struct {bool mqtt_enabled,basic_mode,battery_protect;char mqtt_prefix[64];} settings={true,false,true,"wallbox-ems"};
static bool personal_solis_enabled=true;
static bool mqtt_online=true,restarting,ota_in_progress,boot_validation_failed;
static void *mqtt_client=(void *)1;
static int64_t clock_ms=10000;
static int64_t now_ms(void){return clock_ms;}
static uint32_t esp_random(void){return 0x12345678;}
#define LOCK() do{}while(0)
#define UNLOCK() do{}while(0)
static unsigned sends;
static char last[160];
static int esp_mqtt_client_publish(void *c,const char *t,const char *b,int len,int qos,int retain){assert(c&&strstr(t,"/solis/hold/request")&&!len&&!qos&&!retain);snprintf(last,sizeof(last),"%s",b);sends++;return 1;}
#include "../main/solis_hold_runtime.inc"
int main(void){
 solis_hold_init();assert(strlen(solis_hold_session)==16);
 solis_hold_publish();assert(sends==0&&!solis_hold_wanted);
 solis_hold_ack_locked("{\"ready\":true,\"blocked\":false,\"busy\":false,\"manual\":false,\"ok\":true,\"owned\":false,\"session\":\"\",\"seq\":0}");
 assert(solis_hold_link_locked(clock_ms));solis_hold_publish();assert(sends==0);
 solis_hold_wanted=true;solis_hold_seq=1;solis_hold_publish();assert(sends==1&&strstr(last,"\"hold\":true"));
 mqtt_online=false;clock_ms+=1000;solis_hold_publish();assert(!solis_hold_wanted&&solis_hold_release&&solis_hold_seq==2&&sends==1);
 mqtt_online=true;solis_hold_publish();assert(sends==2&&strstr(last,"\"hold\":false"));
 char ack[256];snprintf(ack,sizeof(ack),"{\"ready\":true,\"blocked\":false,\"busy\":false,\"manual\":false,\"ok\":true,\"owned\":false,\"session\":\"%s\",\"seq\":1}",solis_hold_session);
 solis_hold_ack_locked(ack);assert(solis_hold_release);
 char *seq=strstr(ack,"\"seq\":1");assert(seq);seq[6]='2';solis_hold_ack_locked(ack);assert(!solis_hold_release);
 clock_ms+=20000;solis_hold_publish();assert(sends==2&&!solis_hold_wanted);
 cJSON *o=cJSON_CreateObject();solis_hold_json_locked(o,clock_ms);assert(!cJSON_IsTrue(cJSON_GetObjectItemCaseSensitive(o,"solis_hold_selected")));cJSON_Delete(o);
 puts("Solis opt-in: default off, no startup publish, explicit selection only, disconnect cancels, release acknowledged by session/sequence, no replay after reconnect: OK");
}
