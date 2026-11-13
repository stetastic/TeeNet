#pragma once
#include "cJSON.h"
#include <stdbool.h>
#include <limits.h>
#include <stddef.h>
#include <string.h>

/* These calls consume a newly created/detached item on every path. cJSON's
   AddItem functions leave ownership with the caller when insertion fails. */
static inline cJSON_bool json_add_owned_to_object(cJSON *parent,const char *name,cJSON *item){
    if(cJSON_AddItemToObject(parent,name,item))return true;
    cJSON_Delete(item);return false;
}
static inline cJSON_bool json_add_owned_to_array(cJSON *parent,cJSON *item){
    if(cJSON_AddItemToArray(parent,item))return true;
    cJSON_Delete(item);return false;
}

/* Size the output before printing. cJSON_PrintUnformatted grows its buffer and
   then copies it once more; those simultaneous buffers exhaust small boards. */
static inline size_t json_string_bound(const char *s){
    size_t n=2;
    for(const unsigned char *p=(const unsigned char *)s;p&&*p;p++){
        n+=*p=='"'||*p=='\\'||*p=='\b'||*p=='\f'||*p=='\n'||*p=='\r'||*p=='\t'?2:*p<32?6:1;
        if(n>(size_t)INT_MAX-5)return 0;
    }
    return n;
}
static inline size_t json_output_bound(const cJSON *o,unsigned depth){
    if(!o||depth>16)return 0;
    unsigned type=(unsigned)o->type&255;
    if(type==cJSON_String)return json_string_bound(o->valuestring);
    if(type==cJSON_Raw){size_t n=o->valuestring?strlen(o->valuestring):0;return n&&n<=(size_t)INT_MAX-5?n:0;}
    if(type==cJSON_Number)return 32; /* cJSON uses at most 26 bytes for a number. */
    if(type==cJSON_True||type==cJSON_NULL)return 4;
    if(type==cJSON_False)return 5;
    if(type!=cJSON_Array&&type!=cJSON_Object)return 0;
    size_t n=2;
    for(const cJSON *p=o->child;p;p=p->next){
        size_t child=json_output_bound(p,depth+1),key=type==cJSON_Object?json_string_bound(p->string):0;
        if(!child||(type==cJSON_Object&&!key))return 0;
        n+=child+key+(type==cJSON_Object?1:0)+(p!=o->child?1:0);
        if(n>(size_t)INT_MAX-5)return 0;
    }
    return n;
}
static inline char *json_print_compact(const cJSON *o){
    size_t bound=json_output_bound(o,0);if(!bound)return NULL;
    char *body=cJSON_malloc(bound+5);if(!body)return NULL;
    if(!cJSON_PrintPreallocated((cJSON *)o,body,(int)(bound+5),false)){cJSON_free(body);return NULL;}
    return body;
}
