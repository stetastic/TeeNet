#include "cJSON.h"
#include "shelly_phase.h"
#include "json_owned.h"
#include <float.h>
#include <math.h>
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
        assert(shelly_phase_feedback(status,&f)&&!f.output&&f.has_uptime&&f.uptime==120);
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
    fail_at=0;assert(shelly_phase_feedback(status,&f));assert(used==0);
    cJSON *tree=cJSON_CreateObject(),*numbers=cJSON_AddArrayToObject(tree,"numbers");
    cJSON_AddStringToObject(tree,"quote\"\\\n","\"\\\b\f\n\r\t\001\037 UTF-8: äöü");
    double values[]={0,-0.0,DBL_MIN,DBL_MAX,-DBL_MAX,1e-300,-1e300,NAN,INFINITY};
    for(unsigned i=0;i<sizeof(values)/sizeof(*values);i++){
        cJSON *number=cJSON_CreateNumber(isnan(values[i])?0:values[i]);number->valuedouble=values[i];cJSON_AddItemToArray(numbers,number);
    }
    cJSON_AddNullToObject(tree,"null");cJSON_AddBoolToObject(tree,"true",true);cJSON_AddBoolToObject(tree,"false",false);
    cJSON_AddRawToObject(tree,"raw","[1,2,3]");
    char long_text[12000];memset(long_text,'x',sizeof(long_text)-1);long_text[sizeof(long_text)-1]=0;cJSON_AddStringToObject(tree,"long",long_text);
    char *legacy=cJSON_PrintUnformatted(tree),*bounded=json_print_compact(tree);assert(legacy&&bounded&&!strcmp(legacy,bounded));
    cJSON_free(legacy);cJSON_free(bounded);size_t retained=used;
    calls=0;fail_at=1;assert(!json_print_compact(tree)&&used==retained);fail_at=0;
    cJSON_Delete(tree);assert(!used);assert(!json_print_compact(NULL));
    cJSON_InitHooks(NULL);
    puts("PASS: one-buffer JSON output matches cJSON including escaped strings, long output, number extremes and failed allocation");
    printf("PASS: bounded JSON depth; 20000 feedback cycles without retained allocations; %zu allocation failures; peak %zu bytes\n",count,peak);
}
