#pragma once
#include "cJSON.h"
#include <stdbool.h>

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
