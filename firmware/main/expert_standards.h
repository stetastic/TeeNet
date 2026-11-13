#pragma once
#include "expert_params.h"
#include "hardware_profile.h"
#include <math.h>
static bool expert_standard_set[EP_COUNT],gpio_standard_set[EMS_GPIO_ROLES];
static float expert_standard_saved[EP_COUNT];
static int gpio_standard_saved[EMS_GPIO_ROLES];
static inline float expert_standard_value(unsigned p){return expert_standard_set[p]?expert_standard_saved[p]:expert_meta[p].standard;}
static inline int gpio_standard_value(unsigned p){return gpio_standard_set[p]?gpio_standard_saved[p]:gpio_roles[p].standard;}
static inline bool expert_standards_modified(const expert_values_t *values,const settings_t *cfg){
 for(unsigned p=0;p<EP_COUNT;p++)if(fabsf(values->values[p]-expert_standard_value(p))>.0001f)return true;
 for(unsigned p=0;p<EMS_GPIO_ROLES;p++)if((p==EMS_GPIO_ROLES-1||settings_gpio_used(cfg,p))&&settings_gpio_value(cfg,p)!=gpio_standard_value(p))return true;
 return false;
}
