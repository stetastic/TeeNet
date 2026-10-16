#include "ems_core.h"
#include <assert.h>
#include <math.h>
#include <string.h>
#include <stdio.h>
static void near(float a,float b){assert(fabsf(a-b)<.01f);}
int main(void){
 settings_t s,loaded; settings_defaults(&s);assert(s.fixed_charge_phases==3);
 /* Old padding must not become the new phase selection. */
 s.version=19;size_t old_length=(offsetof(settings_t,fixed_charge_phases)+3)&~(size_t)3;
 unsigned char blob[sizeof(s)];memcpy(blob,&s,sizeof(s));memset(blob+offsetof(settings_t,fixed_charge_phases),0xff,old_length-offsetof(settings_t,fixed_charge_phases));
 assert(settings_decode(blob,old_length,&loaded));assert(loaded.fixed_charge_phases==3 && loaded.charge_phases==3);
 settings_defaults(&s);s.min_charge_a=8;s.fixed_charge_phases=1;s.charge_phases=s.manual_phases=1;s.enabled=true;s.mode=MODE_MANUAL;s.current_offset_a=0;
 assert(settings_valid(&s));assert(settings_decode(&s,sizeof(s),&loaded));assert(!loaded.enabled&&loaded.charge_phases==1&&loaded.manual_phases==1);
 float a[]={8,8,8};near(estimated_charge_power(&s,a),1840);near(solar_current(&s,a,920),4);
 near(charge_plan_maximum_kw(&s,true,false),3.68f);
 s.manual_current_a=6;near(control_target(&s,true,true,true),0);
 s.manual_current_a=8;near(control_target(&s,true,true,true),8);
 float out[3];control_report(&s,a,8,0,true,out);near(out[0],s.grid_limit_a);
 near(out[1],0);near(out[2],0);
 control_report(&s,a,0,0,false,out);near(out[0],49);near(out[1],0);near(out[2],0);
 float only_l1[]={8,NAN,NAN};control_report(&s,only_l1,8,0,false,out);near(out[0],32);near(out[1],0);near(out[2],0);
 /* Both one-phase modes retain the commissioned actuator compensation. */
 s.mode=MODE_PV;s.current_offset_a=2;control_report(&s,only_l1,8,0,false,out);near(out[0],34);
 float low_l1[]={7.9f,NAN,NAN};control_report(&s,low_l1,8.7f,0,false,out);near(out[0],33.2f);
 s.mode=MODE_MANUAL;s.current_offset_a=.8f;
 control_report(&s,only_l1,8.7f,0,false,out);near(out[0],32);
 control_report(&s,only_l1,8,0,false,out);near(out[0],32.8f);
 manual_report_filter_t filter={.active=true,.last_ms=1000,.reported={20,30,40}};
 out[0]=32;out[1]=out[2]=35;manual_report_smooth(&filter,&s,8,3000,out);near(out[1],0);near(out[2],0);
 s.grid_guard_enabled=true;float house[]={45,80,90};grid_guard_report(&s,true,house,out);near(out[0],45);near(out[1],0);near(out[2],0);
 out[1]=7;out[2]=9;grid_guard_report(&s,false,house,out);near(out[1],0);near(out[2],0);
 s.grid_guard_enabled=false;

 /* Single-phase cold start has no idle-load ramp; a reduction must never
    linger above a fresh, falling feedback value near the stop boundary. */
 memset(&filter,0,sizeof(filter));out[0]=24;out[1]=out[2]=24;
 manual_report_smooth(&filter,&s,8.7f,1000,out);near(out[0],32);near(out[1],0);near(out[2],0);
 out[0]=39;manual_report_smooth(&filter,&s,8.7f,3000,out);near(out[0],33);
 out[0]=32.8f;manual_report_smooth(&filter,&s,8.7f,5000,out);near(out[0],32.8f);
 out[0]=32;manual_report_smooth(&filter,&s,8.7f,7000,out);near(out[0],32);
 out[0]=33;manual_report_smooth(&filter,&s,8.7f,9000,out);near(out[0],32.3f);
 out[0]=31;manual_report_smooth(&filter,&s,8.7f,11000,out);near(out[0],32);
 out[0]=31;manual_report_smooth(&filter,&s,8.7f,13000,out);near(out[0],31.4f);
 out[0]=49;manual_report_smooth(&filter,&s,8.7f,15000,out);near(out[0],49);assert(!filter.active);
 out[0]=49;manual_report_smooth(&filter,&s,0,17000,out);near(out[0],49);near(out[1],0);near(out[2],0);

 /* Switching the control policy must not bypass the proven one-phase ramp. */
 manual_report_filter_t manual={0},pv={0};settings_t pv_settings=s;pv_settings.mode=MODE_PV;
 const float sequence[]={24,39,32.8f,32,33,31,31,49};
 for(unsigned i=0;i<sizeof(sequence)/sizeof(sequence[0]);i++){
  float m[]={sequence[i],9,9},v[]={sequence[i],9,9};
  manual_report_smooth(&manual,&s,8.7f,1000+2000*i,m);
  manual_report_smooth(&pv,&pv_settings,8.7f,1000+2000*i,v);
  for(unsigned p=0;p<3;p++)near(m[p],v[p]);
 }

 settings_t before=s;before.fixed_charge_phases=3;assert(!settings_apply_live(&s,&before,&s));
 s.phase_switch_enabled=true;s.relay_board_enabled=true;s.relay1_mode=3;assert(!settings_valid(&s));
 s.phase_switch_enabled=false;s.fixed_charge_phases=2;assert(!settings_valid(&s));
 /* A new bench setup has no meter yet. Merely storing its fixed wiring must
    work; this does not permit charging or any physical phase switch. */
 assert(fixed_wiring_change_allowed(3,1,false,true,false,false,NULL));
 assert(!fixed_wiring_change_allowed(3,1,true,true,false,false,NULL));
 assert(!fixed_wiring_change_allowed(3,1,false,false,false,false,NULL));
 assert(!fixed_wiring_change_allowed(3,2,false,true,false,false,NULL));
 float zero[]={0,0,0};
 assert(fixed_wiring_change_allowed(3,1,false,true,true,true,zero));
 assert(!fixed_wiring_change_allowed(3,1,false,true,true,false,zero));
 zero[1]=1;assert(!fixed_wiring_change_allowed(3,1,false,true,true,true,zero));
 zero[1]=-1;assert(!fixed_wiring_change_allowed(3,1,false,true,true,true,zero));
 zero[1]=NAN;assert(!fixed_wiring_change_allowed(3,1,false,true,true,true,zero));
 assert(fixed_wiring_change_allowed(1,1,true,false,true,false,NULL));
 /* A feedback fault must be repairable at proven zero current; a switch in
    progress, stale/current/invalid samples and external charging stay locked. */
 zero[1]=0;
 assert(hardware_config_change_allowed(false,true,true,true,zero));
 assert(hardware_config_change_allowed(false,true,false,false,NULL));
 assert(!hardware_config_change_allowed(true,true,true,true,zero));
 assert(!hardware_config_change_allowed(false,false,true,true,zero));
 assert(!hardware_config_change_allowed(false,true,true,false,zero));
 assert(phase_setup_change_allowed(false,true,3,true,false,false,false,NULL));
 assert(!phase_setup_change_allowed(false,true,3,true,true,false,false,NULL));
 assert(phase_setup_change_allowed(false,true,1,true,false,false,false,NULL));
 assert(!phase_setup_change_allowed(false,true,3,false,false,false,false,NULL));
 assert(!phase_setup_change_allowed(false,true,3,true,false,true,false,zero));
 assert(phase_setup_change_allowed(false,true,3,true,false,true,true,zero));
 assert(phase_setup_change_allowed(true,false,3,false,false,true,true,zero));
 assert(phase_setup_change_allowed(true,false,3,false,false,false,false,NULL));
 zero[2]=1;assert(!hardware_config_change_allowed(false,true,true,true,zero));
 zero[2]=-1;assert(!hardware_config_change_allowed(false,true,true,true,zero));
 zero[2]=NAN;assert(!hardware_config_change_allowed(false,true,true,true,zero));
 settings_defaults(&s);s.fixed_charge_phases=1;
 assert(control_target(&s,false,false,false)==0);
 puts("PASS: fixed single-phase power, PV and plan ceilings, restart, legacy migration and relay conflict");
 return 0;
}
