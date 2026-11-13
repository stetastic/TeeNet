#include "normal_offset_trial.h"
#include <assert.h>
#include <stdio.h>
#include <string.h>

static void bounded_opt_in(void){
    normal_offset_trial_t trial={0},before=trial;
    assert(normal_offset_trial_ready(&trial,1000)); /* Inactive trial does not block normal operation. */
    assert(!normal_offset_trial_touch(&trial,1000));
    const float invalid[]={NAN,INFINITY,-.1f,2.1f};
    for(unsigned i=0;i<sizeof(invalid)/sizeof(*invalid);i++){
        assert(!normal_offset_trial_begin(&trial,.8f,invalid[i],1000));
        assert(!memcmp(&trial,&before,sizeof(trial)));
    }
    assert(!normal_offset_trial_begin(&trial,NAN,.8f,1000));
    assert(!normal_offset_trial_begin(&trial,INFINITY,.8f,1000));
    assert(!normal_offset_trial_begin(&trial,-.1f,0,1000));
    assert(!normal_offset_trial_begin(&trial,2.1f,2,1000));
    assert(!normal_offset_trial_begin(&trial,.8f,1.01f,1000));
    assert(!normal_offset_trial_begin(&trial,.8f,.59f,1000));
    assert(!memcmp(&trial,&before,sizeof(trial)));
    assert(normal_offset_trial_begin(&trial,.8f,.6f,1000));
    assert(trial.armed&&!trial.fault&&trial.original==.8f&&trial.lease_until==16000);
    before=trial;
    assert(!normal_offset_trial_begin(&trial,.6f,.7f,1100)); /* Never overwrite the restoration value. */
    assert(!memcmp(&trial,&before,sizeof(trial)));
}

static void lease_and_fault(void){
    normal_offset_trial_t trial={0};
    assert(normal_offset_trial_begin(&trial,.8f,.9f,1000));
    assert(normal_offset_trial_ready(&trial,15999));
    assert(!normal_offset_trial_ready(&trial,16000));
    assert(!normal_offset_trial_touch(&trial,16000)); /* Expiry cannot be renewed. */
    assert(!normal_offset_trial_begin(&trial,.8f,.9f,16000));
    trial=(normal_offset_trial_t){0};
    assert(normal_offset_trial_begin(&trial,.8f,1,1000));
    assert(normal_offset_trial_touch(&trial,14000));
    assert(trial.lease_until==29000&&trial.original==.8f);
    assert(normal_offset_trial_ready(&trial,28999));
    assert(!normal_offset_trial_ready(&trial,29000));
    trial.fault=true;
    assert(!normal_offset_trial_ready(&trial,15000));
    assert(!normal_offset_trial_touch(&trial,15000));
    assert(trial.original==.8f);
}

int main(void){
    bounded_opt_in();lease_and_fault();
    puts("PASS: normal offset trial bounds, preserved restoration value, single ownership, lease expiry and fault protection");
}
