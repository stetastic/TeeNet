#include "diagnostic_store.h"
#include <string.h>
#include <stddef.h>
static uint32_t checksum(const diagnostic_store_t *s) {
    const unsigned char *bytes=(const unsigned char *)s;uint32_t hash=2166136261u;
    for(size_t i=0;i<offsetof(diagnostic_store_t,checksum);i++)hash=(hash^bytes[i])*16777619u;
    return hash;
}
void diagnostic_store_init(diagnostic_store_t *s){memset(s,0,sizeof(*s));s->magic=0x544e4447;s->version=1;}
bool diagnostic_store_add(diagnostic_store_t *s,const ems_event_t *event) {
    static const char *faults[]={"boot","boot_watchdog","boot_brownout","meter","feedback","house_meter","battery_stale","wallbox","phase_fault","charge_ended","uart","plan_storage","storage"};
    if(!event||!memchr(event->reason,0,sizeof(event->reason)))return false;
    bool wanted=!strcmp(event->reason,"firmware_check_failed");for(unsigned i=0;i<sizeof(faults)/sizeof(faults[0]);i++)if(!strcmp(event->reason,faults[i]))wanted=true;
    if(!wanted)return false;
    /* State observation already suppresses repeated messages. Boot events stay distinct. */
    s->entries[s->next]=*event;s->next=(s->next+1)%EMS_FAULT_CAPACITY;
    if(s->count<EMS_FAULT_CAPACITY)s->count++;
    return true;
}
void diagnostic_store_seal(diagnostic_store_t *s){s->checksum=checksum(s);}
bool diagnostic_store_restore(diagnostic_store_t *s,const void *data,size_t size) {
    if(!data||size!=sizeof(*s))return false;
    diagnostic_store_t copy;memcpy(&copy,data,size);
    if(copy.magic!=0x544e4447||copy.version!=1||copy.count>EMS_FAULT_CAPACITY||copy.next>=EMS_FAULT_CAPACITY||copy.checksum!=checksum(&copy))return false;
    for(unsigned i=0;i<EMS_FAULT_CAPACITY;i++)if(!memchr(copy.entries[i].reason,0,sizeof(copy.entries[i].reason)))return false;
    *s=copy;return true;
}
const ems_event_t *diagnostic_store_recent(const diagnostic_store_t *s,unsigned index) {
    return index<s->count?&s->entries[(s->next+EMS_FAULT_CAPACITY-1-index)%EMS_FAULT_CAPACITY]:NULL;
}
