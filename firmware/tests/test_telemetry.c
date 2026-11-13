#include "telemetry_core.h"
#include <assert.h>
#include <stdio.h>
static void test_runtime(void){
 telemetry_runtime_t idle={.current_fresh=true,.shell_configured=true,.shell_known=true,.vehicle_state='A',
     .current_a={0.1f,0.2f,0.3f},.internal_free=TELEMETRY_MIN_INTERNAL_FREE,.internal_largest=TELEMETRY_MIN_INTERNAL_BLOCK};
 assert(telemetry_runtime_ready(&idle));assert(!telemetry_runtime_ready(NULL));
 telemetry_runtime_t r;
#define BLOCKED(field,value) do{r=idle;r.field=value;assert(!telemetry_runtime_ready(&r));}while(0)
 BLOCKED(charging_requested,true);BLOCKED(phase_busy,true);BLOCKED(test_active,true);
 BLOCKED(current_fresh,false);BLOCKED(shell_known,false);BLOCKED(session_present,true);
 BLOCKED(internal_free,TELEMETRY_MIN_INTERNAL_FREE-1);BLOCKED(internal_largest,TELEMETRY_MIN_INTERNAL_BLOCK-1);
 BLOCKED(internal_free,0);BLOCKED(internal_largest,0);
 for(const char *s="BCDF?";*s;s++){BLOCKED(vehicle_state,*s);}
 for(unsigned i=0;i<3;i++){
  BLOCKED(current_a[i],1);BLOCKED(current_a[i],12);BLOCKED(current_a[i],-0.1f);
  BLOCKED(current_a[i],NAN);BLOCKED(current_a[i],INFINITY);
 }
 r=idle;r.shell_configured=false;r.shell_known=false;r.vehicle_state='?';assert(telemetry_runtime_ready(&r));
 r.session_present=true;assert(!telemetry_runtime_ready(&r));
 r=idle;r.internal_free=200000;r.internal_largest=100000;r.charging_requested=true;assert(!telemetry_runtime_ready(&r));
#undef BLOCKED
 telemetry_config_t deletion={.schema=1,.delete_pending=1};memset(deletion.id,'a',32);memset(deletion.key,'b',64);
 telemetry_config_t before=deletion;
 assert(telemetry_due(&deletion,false,true,false,100,0));
 r=idle;r.test_active=true;assert(!telemetry_runtime_ready(&r));assert(!memcmp(&deletion,&before,sizeof(deletion)));
 r.test_active=false;assert(telemetry_runtime_ready(&r)&&telemetry_due(&deletion,false,true,false,100,0));
}
int main(void){
 test_runtime();
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
 puts("PASS: consent/timing/deletion preserved; optional TLS deferred for charge requests, live/invalid/stale current, vehicle/session/unknown configured Shell, phase/tests and insufficient internal RAM/block");
}
