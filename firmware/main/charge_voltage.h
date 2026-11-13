#pragma once
#include "ems_core.h"
#include <math.h>
#include <string.h>
/* Voltage and current are committed from the same selected meter transaction.
   No extra network request or flash write is needed. */
typedef struct {float value[3];int64_t at;uint32_t revision;} charge_voltage_t;
static inline void charge_voltage_sample(charge_voltage_t *v,const float values[3],bool valid,uint32_t revision,int64_t now){
 v->at=0;if(!valid||!values)return;
 for(unsigned i=0;i<3;i++)if(!isfinite(values[i])||values[i]<0||values[i]>300)return;
 memcpy(v->value,values,sizeof(v->value));v->revision=revision;v->at=now;
}
static inline bool charge_voltage_get(const charge_voltage_t *v,const settings_t *s,uint32_t revision,bool online,int64_t now,int64_t ttl,float out[3]){
 if(!online||strcmp(s->wallbox_meter_type,"shelly_gen2")||!v->at||v->revision!=revision||now<v->at||now-v->at>=ttl||(s->charge_phases!=1&&s->charge_phases!=3))return false;
 for(unsigned i=0;i<3;i++){
  bool usable=isfinite(v->value[i])&&v->value[i]>=180&&v->value[i]<=270;
  if(i<s->charge_phases&&!usable)return false;
  /* Disconnected phases supply no voltage behind a one-phase contactor.
     Their future three-phase voltage remains a nominal planning estimate. */
  out[i]=usable?v->value[i]:s->nominal_v;
 }
 return true;
}
