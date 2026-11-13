#include "ems_core.h"
#include <assert.h>
#include <math.h>
#include <stdio.h>
static void near(float a,float b){assert(fabsf(a-b)<.001f);}
int main(void){
 settings_t s;settings_defaults(&s);s.mode=MODE_MANUAL;s.min_charge_a=7.5f;
 const float target=7.8f,grid=s.grid_limit_a;manual_report_filter_t f={0};float a[3]={0,0,0},r[3];
 for(int t=1000;t<=5000;t+=1000){control_report(&s,a,target,0,false,r);float original=r[0];manual_report_smooth(&f,&s,target,t,r);near(r[0],original);}
 for(int p=0;p<3;p++)a[p]=15;
 control_report(&s,a,target,0,false,r);manual_report_smooth(&f,&s,target,6000,r);
 for(int p=0;p<3;p++)near(r[p],grid+.3f);
 control_report(&s,a,target,0,false,r);manual_report_smooth(&f,&s,target,10000,r);
 for(int p=0;p<3;p++)near(r[p],grid+.8f); /* delayed poll capped */
 for(int p=0;p<3;p++)a[p]=target;
 control_report(&s,a,target,0,false,r);float compensated=r[0];assert(compensated>grid+.7f);
 manual_report_smooth(&f,&s,target,11000,r);near(r[0],compensated); /* offset stays active */
 for(int p=0;p<3;p++)a[p]=target+.5f;
 control_report(&s,a,target,0,false,r);manual_report_smooth(&f,&s,target,12000,r);
 near(r[0],compensated+.02f); /* smaller near-goal reduction */
 for(int p=0;p<3;p++)a[p]=target-.5f;
 control_report(&s,a,target,0,false,r);float release=r[0];manual_report_smooth(&f,&s,target,13000,r);near(r[0],release);
 control_report(&s,a,0,0,false,r);manual_report_smooth(&f,&s,0,14000,r);assert(!f.active);near(r[0],grid+s.max_charge_a+1);
 puts("PASS: idle start unchanged, fast bounded bulk correction, small near-goal steps, persistent offset, immediate release and Stop");
}
