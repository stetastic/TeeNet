#pragma once
#include <math.h>
#include <stdbool.h>
#include <stdint.h>

#define PHASE_VOLTAGE_TTL_MS 3500
typedef struct {
    float voltage[3]; /* Logical wallbox L1/L2/L3, not Shelly A/B/C. */
    unsigned candidate,position,samples;
    int64_t since,at,sample_at;
} phase_voltage_proof_t;

static inline unsigned phase_voltage_position(const float voltage[3]){
    for(unsigned i=0;i<3;i++)if(!isfinite(voltage[i])||voltage[i]<0||voltage[i]>300)return 0;
    if(voltage[0]<180||voltage[0]>270)return 0;
    if(voltage[1]<=50&&voltage[2]<=50)return 1;
    if(voltage[1]>=180&&voltage[1]<=270&&voltage[2]>=180&&voltage[2]<=270)return 3;
    return 0; /* Missing/partial/ambiguous voltages never confirm a position. */
}
static inline void phase_voltage_sample(phase_voltage_proof_t *proof,const float logical[3],bool valid,int64_t now){
    unsigned position=valid?phase_voltage_position(logical):0;
    bool continuous=proof->at>0&&now>proof->at&&now-proof->at<=PHASE_VOLTAGE_TTL_MS;
    proof->sample_at=valid?now:0;
    if(valid)for(unsigned i=0;i<3;i++)proof->voltage[i]=logical[i];
    if(!position){proof->candidate=proof->position=proof->samples=0;proof->at=0;return;}
    if(!continuous||proof->candidate!=position){proof->candidate=position;proof->since=now;proof->samples=0;proof->position=0;}
    proof->at=now;if(proof->samples<3)proof->samples++;
    if(proof->samples>=2&&now-proof->since>=500)proof->position=position;
}
static inline unsigned phase_voltage_observed(const phase_voltage_proof_t *proof,int64_t now){
    return proof->at>0&&now>=proof->at&&now-proof->at<PHASE_VOLTAGE_TTL_MS?proof->position:0;
}
