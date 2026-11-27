#include "telemetry_core.h"
#include <assert.h>
#include <stdio.h>
static bool telemetry_runtime_ready(const telemetry_runtime_t *r){return telemetry_runtime_block(r)==NULL;}
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
 r=idle;r.vehicle_state='B';r.shell_reports_idle=true;r.session_present=true;r.internal_largest=31744;
 assert(telemetry_runtime_ready(&r));r.vehicle_state='D';assert(telemetry_runtime_ready(&r));
 r.current_a[2]=1;assert(!telemetry_runtime_ready(&r));r.current_a[2]=0;
 r.vehicle_state='C';assert(!telemetry_runtime_ready(&r));r.vehicle_state='B';r.shell_known=false;assert(!telemetry_runtime_ready(&r));
#undef BLOCKED
 telemetry_config_t deletion={.schema=1,.delete_pending=1};memset(deletion.id,'a',32);memset(deletion.key,'b',64);
 telemetry_config_t before=deletion;
 assert(telemetry_due(&deletion,false,true,false,100,0));
 r=idle;r.test_active=true;assert(!telemetry_runtime_ready(&r));assert(!memcmp(&deletion,&before,sizeof(deletion)));
 r.test_active=false;assert(telemetry_runtime_ready(&r)&&telemetry_due(&deletion,false,true,false,100,0));
}
static void test_standby(void){
 telemetry_idle_window_t window={0};
 int64_t first=1000,last=0;
 for(int64_t now=first;now<=first+TELEMETRY_STANDBY_MS;now+=1000){
  telemetry_idle_sample(&window,last,now,5000,true,true);
  assert(telemetry_long_idle(window,now,true)==(now-first>=TELEMETRY_STANDBY_MS));
  last=now;
 }
 assert(!telemetry_long_idle(window,last,false));
 telemetry_idle_sample(&window,last,last+5000,5000,true,true);
 assert(!telemetry_long_idle(window,last+5000,true));
 telemetry_idle_sample(&window,last+5000,last+6000,5000,true,false);assert(window.since==0);
 assert(telemetry_http_idle('A',"Available",false,false,0));
 assert(telemetry_http_idle('B',"SuspendedEVSE",true,true,.1f));
 assert(!telemetry_http_idle('B',"SuspendedEV",true,false,0));
 assert(!telemetry_http_idle('C',"Charging",true,true,0));
 assert(!telemetry_http_idle('B',"idle",true,true,1));
 assert(!telemetry_http_idle('A',"",false,false,0));
 telemetry_runtime_t r={.charging_requested=true,.current_fresh=true,.long_meter_idle=true,
   .shell_configured=true,.shell_known=true,.vehicle_state='B',.session_present=true,
   .internal_free=TELEMETRY_MIN_INTERNAL_FREE,.internal_largest=TELEMETRY_MIN_INTERNAL_BLOCK};
 assert(telemetry_runtime_ready(&r));
 r.phase_busy=true;assert(!telemetry_runtime_ready(&r));r.phase_busy=false;
 r.vehicle_state='C';assert(!telemetry_runtime_ready(&r));r.vehicle_state='B';
 r.current_fresh=false;assert(!telemetry_runtime_ready(&r));
 r.long_http_idle=true;assert(telemetry_runtime_ready(&r));
 r.current_fresh=true;r.current_a[1]=2;assert(!telemetry_runtime_ready(&r));
}
int main(void){
 test_standby();
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
 puts("PASS: standby with active charge request requires five minutes of verified idle; consent, charging, freshness, phase and memory checks passed");
}
