#include "telemetry_core.h"
#include <assert.h>
#include <stdio.h>
int main(void){
 telemetry_config_t c={.schema=1};assert(telemetry_valid(&c));
 assert(!telemetry_due(&c,true,true,false,100,0));
 memset(c.id,'a',32);memset(c.key,'b',64);strcpy(c.revision,TELEMETRY_REVISION);c.enabled=1;
 assert(telemetry_valid(&c));assert(!telemetry_due(&c,false,true,false,100,0));
 assert(!telemetry_due(&c,true,false,false,100,0));assert(!telemetry_due(&c,true,true,true,100,0));
 assert(!telemetry_due(&c,true,true,false,100,101));assert(telemetry_due(&c,true,true,false,101,101));
 strcpy(c.revision,"old");assert(!telemetry_valid(&c));strcpy(c.revision,TELEMETRY_REVISION);
 telemetry_revoke(&c);assert(!c.enabled&&c.delete_pending&&telemetry_valid(&c));
 assert(telemetry_due(&c,false,true,false,100,0));assert(!telemetry_due(&c,false,false,false,100,0));
 c.enabled=1;assert(!telemetry_valid(&c));c.enabled=0;c.key[5]='X';assert(!telemetry_valid(&c));
 c=(telemetry_config_t){.schema=1};telemetry_revoke(&c);assert(!c.delete_pending&&!c.enabled);
 puts("PASS: no heartbeat without explicit consent; reboot-safe credentials, timing, maintenance pause and revocation/deletion gates");
}
