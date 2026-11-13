#include "charge_counter.h"
#include <math.h>
#include <string.h>

static uint64_t key(const char *text){
    uint64_t result=UINT64_C(14695981039346656037);
    if(text)for(const unsigned char *p=(const unsigned char *)text;*p;p++){result^=*p;result*=UINT64_C(1099511628211);}
    return result;
}
bool charge_counter_checkpoint_valid(const charge_counter_checkpoint_t *s){
    return s&&s->version==1&&isfinite(s->raw_wh)&&s->raw_wh>=0&&s->raw_wh<=1000000&&s->epoch_ms>=0;
}
charge_counter_delta_t charge_counter_step(charge_counter_t *c,bool enabled,const charge_counter_input_t *i){
    charge_counter_delta_t out={0};
    if(!c||!i)return out;
    c->enabled=enabled;
    if(!enabled){memset(&c->saved,0,sizeof(c->saved));c->saved.version=1;c->sample_ms=0;c->restored=false;c->ready=false;return out;}
    if(!i->fresh){c->gap=true;c->ready=false;return out;}
    if(i->sample_ms<=0||i->sample_ms>i->now_ms||(!c->restored&&i->sample_ms<=c->sample_ms))return out;
    int64_t at_epoch=i->epoch_ms>0?i->epoch_ms-(i->now_ms-i->sample_ms):0;
    if(c->restored&&c->saved.anchored&&c->saved.epoch_ms>0&&at_epoch<=0){c->gap=true;c->ready=false;return out;}
    if(i->idle&&!i->present){c->saved.idle_seen=true;c->saved.epoch_ms=at_epoch;c->sample_ms=i->sample_ms;c->restored=false;c->ready=false;return out;}
    if(!i->present||!i->energy_valid||!isfinite(i->raw_wh)||i->raw_wh<0||i->raw_wh>1000000){c->gap=true;c->ready=false;return out;}
    uint64_t host=key(i->host);bool explicit_id=i->identity&&i->identity[0];
    uint64_t session=key(explicit_id?i->identity:i->card);
    if(c->saved.anchored&&c->saved.host_key==host&&c->saved.explicit_id&&!explicit_id){c->gap=true;c->ready=false;return out;}
    int64_t elapsed=c->restored?at_epoch-c->saved.epoch_ms:i->sample_ms-c->sample_ms;
    bool changed=c->saved.anchored&&(host!=c->saved.host_key||session!=c->saved.session_key||explicit_id!=c->saved.explicit_id);
    bool first=!c->saved.anchored;
    out.new_session=changed;
    double delta=0;
    if(first){if(c->saved.idle_seen){delta=i->raw_wh;out.new_session=true;}}
    else if(host!=c->saved.host_key){out.gap=true;}
    else if(changed||c->saved.idle_seen){delta=i->raw_wh;out.new_session=true;}
    else if(i->raw_wh+.001<c->saved.raw_wh){c->resets++;out.gap=true;}
    else delta=fmax(0,i->raw_wh-c->saved.raw_wh);
    /* Never turn an identity change, reset or implausible jump into phantom kWh.
       A 44-kW bound includes polling/rounding tolerance above the supported 22 kW. */
    if(delta>0&&(elapsed<=0||elapsed>7LL*86400000||delta>44000.0*elapsed/3600000.0+2)){
        delta=0;c->rejected++;out.gap=true;
    }
    out.wh=delta;out.to_ms=i->sample_ms;out.from_ms=c->restored?i->sample_ms-fmax(0,elapsed):c->sample_ms;
    out.from_epoch_ms=c->saved.epoch_ms;out.to_epoch_ms=at_epoch;
    out.gap=out.gap||c->gap||c->restored||(elapsed>15000);
    c->saved=(charge_counter_checkpoint_t){.version=1,.host_key=host,.session_key=session,.raw_wh=i->raw_wh,.epoch_ms=at_epoch,.anchored=true,.explicit_id=explicit_id};
    c->sample_ms=i->sample_ms;c->restored=false;c->gap=false;c->ready=true;
    return out;
}
