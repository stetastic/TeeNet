#pragma once
#include "charge_sessions.h"

/* An atomic RAM copy contains retained rows only, oldest first. Disk slot
   numbers remain unchanged, so existing two-bank session storage is compatible. */
typedef struct {
    uint32_t count,next_slot,next_id,active_slot;
    charge_session_t records[];
} session_checkpoint_t;
static inline bool session_checkpoint_copy(session_checkpoint_t *out,unsigned capacity,const session_store_t *store){
    if(!out||store->count>capacity||store->count>CHARGE_SESSION_CAPACITY||store->next_slot>=CHARGE_SESSION_CAPACITY)return false;
    out->count=store->count;out->next_slot=store->next_slot;out->next_id=store->next_id;out->active_slot=store->active_slot;
    unsigned first=(store->next_slot+CHARGE_SESSION_CAPACITY-store->count)%CHARGE_SESSION_CAPACITY;
    for(unsigned i=0;i<store->count;i++)out->records[i]=store->records[(first+i)%CHARGE_SESSION_CAPACITY];
    return true;
}
static inline const charge_session_t *session_checkpoint_at(const session_checkpoint_t *copy,unsigned slot){
    unsigned first=(copy->next_slot+CHARGE_SESSION_CAPACITY-copy->count)%CHARGE_SESSION_CAPACITY;
    unsigned offset=(slot+CHARGE_SESSION_CAPACITY-first)%CHARGE_SESSION_CAPACITY;
    return offset<copy->count?&copy->records[offset]:NULL;
}
