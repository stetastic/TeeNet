#include "ems_core.h"
#include <assert.h>
#include <math.h>
#include <stdio.h>

static void advance(pv_control_t *c,int64_t from,int64_t to,float available,bool permitted) {
    for(int64_t now=from;now<=to;now+=1000) pv_control_step(c,permitted,available,8,16,now);
}
int main(void) {
    pv_control_t c={0};
    /* Thirty seconds of continuous surplus; a short interruption restarts it. */
    advance(&c,0,29000,9,true); assert(c.target_a==0 && c.wait_ms==1000);
    advance(&c,30000,30000,8.9f,true); assert(c.phase==PV_WAITING);
    advance(&c,31000,60000,9,true); assert(c.target_a==0);
    pv_control_step(&c,true,9,8,16,60999); assert(c.target_a==0 && c.wait_ms==1);
    advance(&c,61000,61000,9,true); assert(c.target_a==9 && c.phase==PV_RUNNING);
    /* The established ramp and stop behavior must remain unchanged. */
    advance(&c,62000,65000,16,true); assert(c.target_a==9);
    advance(&c,66000,66000,16,true); assert(c.target_a==10);
    advance(&c,67000,71000,16,true); assert(c.target_a==11);
    advance(&c,72000,81000,16,true); assert(c.target_a==13);
    /* A cloud lasting less than 30 s reduces current but does not restart the car. */
    advance(&c,82000,110000,0,true); assert(c.target_a==8 && c.phase==PV_STOPPING);
    advance(&c,111000,111000,9,true); assert(c.target_a>0 && c.phase==PV_RUNNING);
    advance(&c,112000,141000,0,true); assert(c.target_a==8);
    advance(&c,142000,142000,0,true); assert(c.target_a==0 && c.phase==PV_COOLDOWN && c.wait_ms==120000);
    /* Start validation runs during cooldown, without another 30-second wait. */
    advance(&c,143000,261000,16,true); assert(c.target_a==0 && c.phase==PV_COOLDOWN);
    advance(&c,262000,262000,16,true); assert(c.target_a==9 && c.phase==PV_RUNNING);
    advance(&c,263000,292000,16,true);
    /* Stop or a sensor failure must bypass the sun/cloud delay immediately. */
    advance(&c,293000,293000,16,false); assert(c.target_a==0 && c.phase==PV_BLOCKED);
    advance(&c,294000,294000,16,true); assert(c.target_a==0 && c.phase==PV_STARTING);
    advance(&c,295000,323000,16,true); assert(c.target_a==0);
    advance(&c,324000,324000,16,true); assert(c.target_a==9);
    advance(&c,325000,325000,NAN,true); assert(c.target_a==0 && c.phase==PV_BLOCKED);
    /* Threshold noise for ten minutes never starts charging. */
    pv_control_t noise={0};
    for(int64_t t=0;t<=600000;t+=1000) {
        pv_control_step(&noise,true,t%2000?8.3f:7.7f,8,16,t); assert(noise.target_a==0);
    }
    /* While running, short dips never switch the contactor off. */
    pv_control_t clouds={0}; advance(&clouds,0,60000,16,true);
    for(int64_t t=61000;t<=360000;t+=1000) {
        pv_control_step(&clouds,true,(t/10000)%2?7.4f:9.0f,8,16,t); assert(clouds.target_a>=8);
    }
    pv_control_step(&clouds,true,16,8,16,370000); assert(clouds.target_a==0); /* stalled controller */
    pv_control_step(&clouds,true,16,8,16,1000); assert(clouds.target_a==0 && clouds.phase==PV_STARTING);
    /* A minimum equal to maximum can still start, always within configured bounds. */
    pv_control_t fixed={0};
    for(int64_t t=0;t<=60000;t+=1000) pv_control_step(&fixed,true,8,8,8,t);
    assert(fixed.target_a==8); pv_control_step(&fixed,true,INFINITY,8,8,61000); assert(fixed.target_a==0);
    float idle[]={.2f,.2f,.2f},slow[]={7,7,7};
    assert(pv_ramp_available(16,8.1f,idle)==8.1f);
    assert(pv_ramp_available(16,8.1f,slow)==8.1f);
    assert(pv_ramp_available(6,8.1f,slow)==6);
    assert(pv_ramp_available(16,0,idle)==16);
    /* Live limits override the adjustment interval and deadband. Otherwise a
       0.1 A maximum reduction can leave an out-of-range target indefinitely,
       which the Shell report correctly treats as a stop request. */
    pv_control_t limited={0};advance(&limited,0,100000,16,true);
    assert(limited.target_a==16);
    pv_control_step(&limited,true,15.9f,8,15.9f,101000);
    assert(fabsf(limited.target_a-15.9f)<.001f&&limited.phase==PV_RUNNING);
    pv_control_step(&limited,true,12,8,12,102000);
    assert(limited.target_a==12&&limited.phase==PV_RUNNING);
    puts("PASS: PV start delay, cloud bridging, stop delay, cooldown, ramp limit, threshold noise, immediate stop/fault, clock and scheduler faults");
    return 0;
}
