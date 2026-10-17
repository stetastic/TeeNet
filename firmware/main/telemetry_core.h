#pragma once
#include <stdbool.h>
#include <stdint.h>
#include <string.h>

#define TELEMETRY_REVISION "2026-10-16.1"
#define TELEMETRY_INTERVAL_MS 300000LL
typedef struct {
    uint32_t schema;
    uint8_t enabled,delete_pending,reserved[2];
    char revision[24],id[33],key[65];
} telemetry_config_t;
static inline bool telemetry_hex(const char *s,unsigned n){
    for(unsigned i=0;i<n;i++)if(!((s[i]>='0'&&s[i]<='9')||(s[i]>='a'&&s[i]<='f')))return false;
    return !s[n];
}
static inline bool telemetry_valid(const telemetry_config_t *c){
    if(c->schema!=1||c->enabled>1||c->delete_pending>1||c->revision[23]||c->id[32]||c->key[64])return false;
    if(c->enabled&&c->delete_pending)return false;
    if(!c->enabled&&!c->delete_pending&&!c->id[0]&&!c->key[0])return true;
    return telemetry_hex(c->id,32)&&telemetry_hex(c->key,64)&&(!c->enabled||!strcmp(c->revision,TELEMETRY_REVISION));
}
static inline bool telemetry_due(const telemetry_config_t *c,bool agreed,bool online,bool paused,int64_t now,int64_t next){
    return telemetry_valid(c)&&online&&!paused&&now>=next&&(c->delete_pending||(c->enabled&&agreed));
}
static inline void telemetry_revoke(telemetry_config_t *c){
    c->enabled=0;c->delete_pending=telemetry_hex(c->id,32)&&telemetry_hex(c->key,64);
}
