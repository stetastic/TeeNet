#include "sungrow_modbus.h"
#include <math.h>

static uint16_t u16(const uint8_t *p){return (uint16_t)((uint16_t)p[0]<<8|p[1]);}
static uint32_t u32(const uint8_t *p){return (uint32_t)u16(p)|(uint32_t)u16(p+2)<<16;}
bool sungrow_reply_header(const uint8_t h[9],uint16_t tx,uint8_t unit,uint16_t count,uint8_t *exception){
    *exception=0;
    if(!count||count>125||u16(h)!=tx||u16(h+2)||h[6]!=unit)return false;
    if(h[7]==0x84&&u16(h+4)==3){*exception=h[8];return false;}
    return h[7]==4&&h[8]==count*2&&u16(h+4)==count*2+3;
}
bool sungrow_power_decode(const uint8_t *p,size_t n,bool sign,float scale,float *w){
    if(!p||n!=4||!isfinite(scale)||!scale)return false;
    uint32_t raw=u32(p);
    if(raw==UINT32_MAX||raw==0x7fffffffu||raw==0x80000000u)return false;
    double value=(sign?(double)(int32_t)raw:(double)raw)*scale;
    if(!isfinite(value)||fabs(value)>5000000)return false;
    *w=(float)value;return true;
}
static bool battery(float net,uint16_t soc,sungrow_reading_t *r){
    if(soc>1000||!isfinite(net)||fabsf(net)>500000)return false;
    r->soc=soc*.1f;r->charge_w=fmaxf(0,-net);r->discharge_w=fmaxf(0,net);return true;
}
bool sungrow_sh_decode(const uint8_t *p,size_t n,sungrow_reading_t *r){
    /* Input 13001..13023: flow flags, export power, battery current/power/SOC. */
    if(!p||n!=46||!r)return false;
    r->grid_ok=sungrow_power_decode(p+18,4,true,-1,&r->grid_w);
    uint16_t flow=u16(p),power=u16(p+42),soc=u16(p+44);
    bool charging=(flow&2)!=0,discharging=(flow&4)!=0;
    /* Contradictory direction or unavailable values never grant battery support. */
    r->battery_ok=flow!=0xffff&&!(charging&&discharging)&&power!=0xffff&&
        (power==0||charging||discharging)&&battery(charging?-(float)power:(float)power,soc,r);
    return r->grid_ok||r->battery_ok;
}
bool sungrow_ihm_decode(const uint8_t *p,size_t n,sungrow_reading_t *r){
    if(!p||n!=18||!r)return false;
    r->pv_ok=sungrow_power_decode(p,4,true,10,&r->pv_w)&&r->pv_w>=0;
    r->grid_ok=sungrow_power_decode(p+4,4,true,10,&r->grid_w);
    float net;
    r->battery_ok=sungrow_power_decode(p+12,4,true,10,&net)&&battery(net,u16(p+16),r);
    return r->grid_ok||r->pv_ok||r->battery_ok;
}
