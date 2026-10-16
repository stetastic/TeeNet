#include "cJSON.h"
#include "shelly_phase.h"
#include <assert.h>
#include <stddef.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

typedef union { max_align_t alignment; size_t bytes; } allocation_t;
static size_t used,peak,calls,fail_at;
static void *tracked_alloc(size_t size){
    if(++calls==fail_at)return NULL;
    allocation_t *p=malloc(sizeof(*p)+size);if(!p)return NULL;
    p->bytes=size;used+=size;if(used>peak)peak=used;return p+1;
}
static void tracked_free(void *ptr){
    if(!ptr)return;
    allocation_t *p=(allocation_t *)ptr-1;assert(used>=p->bytes);used-=p->bytes;free(p);
}
int main(void){
    cJSON_Hooks hooks={tracked_alloc,tracked_free};cJSON_InitHooks(&hooks);
    char nested[520];memset(nested,'[',256);nested[256]='0';memset(nested+257,']',256);nested[513]=0;
    cJSON *j=cJSON_ParseWithOpts(nested,NULL,true);
    assert(!j&&used==0); /* A small network response cannot recurse 256 levels. */
    const char *status="{\"switch:0\":{\"id\":0,\"output\":false},\"input:0\":{\"id\":0,\"state\":true},\"input:1\":{\"id\":1,\"state\":false},\"sys\":{\"uptime\":120}}";
    for(unsigned n=0;n<20000;n++){
        shelly_phase_feedback_t f;
        assert(shelly_phase_feedback(status,&f)&&f.phases==3);
        assert(!shelly_phase_feedback("{\"switch:0\":",&f));
        assert(shelly_output_ack("{\"was_on\":false}"));
        assert(!shelly_output_ack("{\"was_on\":false,\"was_on\":true}"));
        assert(used==0);
    }
    /* Every single allocation failure in this real parser path releases all
       prior allocations and refuses to promote partial data to feedback. */
    calls=0;shelly_phase_feedback_t f;assert(shelly_phase_feedback(status,&f));size_t count=calls;
    for(size_t at=1;at<=count;at++){
        calls=0;fail_at=at;assert(!shelly_phase_feedback(status,&f));assert(used==0);
    }
    fail_at=0;assert(shelly_phase_feedback(status,&f));assert(used==0);cJSON_InitHooks(NULL);
    printf("PASS: bounded JSON depth; 20000 feedback cycles without retained allocations; %zu allocation failures; peak %zu bytes\n",count,peak);
}
