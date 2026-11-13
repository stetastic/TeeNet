#pragma once
#include <stdbool.h>
#include <stdint.h>
#include <string.h>
#include <math.h>
#include <stddef.h>

#define TELEMETRY_REVISION "2026-10-16.1"
#define TELEMETRY_INTERVAL_MS 300000LL
/* TLS uses internal RAM (16 KiB receive + 4 KiB send buffers, certificates,
   handshake and network allocations). Optional traffic must leave headroom
   for control and measurements; PSRAM does not satisfy this reserve. */
#define TELEMETRY_MIN_INTERNAL_FREE (96u*1024u)
#define TELEMETRY_MIN_INTERNAL_BLOCK (32u*1024u)
typedef struct {
    bool charging_requested,phase_busy,test_active,current_fresh;
    bool shell_configured,shell_known,session_present;
    char vehicle_state;
    float current_a[3];
    size_t internal_free,internal_largest;
} telemetry_runtime_t;
static inline bool telemetry_runtime_ready(const telemetry_runtime_t *r){
    if(!r||r->charging_requested||r->phase_busy||r->test_active||!r->current_fresh||
       r->session_present||(r->shell_configured&&(!r->shell_known||r->vehicle_state!='A'))||
       r->vehicle_state=='B'||r->vehicle_state=='C'||r->vehicle_state=='D'||r->vehicle_state=='F'||
       r->internal_free<TELEMETRY_MIN_INTERNAL_FREE||r->internal_largest<TELEMETRY_MIN_INTERNAL_BLOCK)return false;
    for(unsigned i=0;i<3;i++)if(!isfinite(r->current_a[i])||r->current_a[i]<0||r->current_a[i]>=1)return false;
    return true;
}
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
