#include "charge_curve.h"
#include <assert.h>
#include <math.h>
#include <stdio.h>
#include <string.h>

int main(void) {
    charge_curve_t c={0},saved={0},restored={0};
    charge_curve_step(&c,10,1700000010,true,false,true,false,0,6000);
    assert(!c.count); /* Waiting for a car is not a charged session. */
    charge_curve_step(&c,20,1700000020,true,false,true,true,6100,6000);
    assert(c.active&&c.count==1&&c.points[0].s==0&&charge_curve_valid(&c));
    charge_curve_step(&c,25,1700000025,true,false,false,false,0,6000);
    assert(isnan(c.live.w)&&c.active); /* Missing data is a gap, not zero/end. */
    charge_curve_step(&c,30,1700000030,true,false,true,false,0,0);
    assert(c.active); /* PV pause retains the same session. */
    for(uint32_t i=35;i<=86420;i++) {
        float watts=i==101?12345:6000;
        charge_curve_step(&c,i,1700000000+i,true,false,true,true,watts,6000);
        assert(c.count<=CHARGE_CURVE_POINTS);
    }
    assert(c.duration_s==86400&&c.points[0].s==0&&c.interval_s>5&&charge_curve_valid(&c));
    bool peak=false,gap=false;
    for(uint32_t i=0;i<c.count;i++){if(c.points[i].high_w>=12345)peak=true;if(isnan(c.points[i].w))gap=true;}
    assert(peak&&gap); /* Compaction retains measured extrema and gaps. */
    saved=c;assert(charge_curve_restore(&restored,&saved));
    assert(!restored.active&&restored.interrupted&&restored.duration_s==86400);
    saved.count=CHARGE_CURVE_POINTS+1;assert(!charge_curve_restore(&restored,&saved));
    charge_curve_step(&c,86425,1700086425,false,false,true,false,0,0);
    assert(!c.active&&!c.interrupted&&c.end_epoch==1700086425&&charge_curve_valid(&c));
    uint32_t revision=c.revision;
    charge_curve_step(&c,86430,1700086430,false,false,true,false,0,0);
    assert(c.revision==revision&&c.duration_s==86405); /* Finished curve stays. */
    charge_curve_step(&c,86435,1700086435,true,false,true,true,8000,8000);
    assert(c.active&&c.count==1&&c.duration_s==0&&c.start_epoch==1700086435);
    charge_curve_step(&c,86440,1700086440,true,true,true,false,0,8000);
    assert(!c.active); /* Car full also closes a session while enabled. */
    memset(&c,0,sizeof(c));charge_curve_step(&c,100,0,true,false,true,true,6000,6000);
    charge_curve_step(&c,110,1700000010,true,false,true,true,6000,6000);
    assert(c.start_epoch==1700000000); /* Late time sync anchors the start. */
    puts("PASS: full-session curve, 24-hour bounded compression, extrema, gaps, PV pause, completion, restore and new session");
}
