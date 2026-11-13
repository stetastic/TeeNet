#include "vehicle_profiles.h"
#include "ems_features.h"
#include <assert.h>
#include <math.h>
#include <stdio.h>
#include <string.h>
int main(void){
    vehicle_profile_t p={.name="Testauto",.plate="AB-CD 123",.cards={"ABC123","SECOND"},.capacity_kwh=60,.maximum_kw=11,.efficiency=.9f,.target_soc=80};
    vehicle_store_t store={.version=1,.enabled=true};store.rows[0]=p;
    assert(vehicle_store_valid(&store)&&vehicle_match(&store," abc123 ")==0&&vehicle_match(&store,"SECOND")==0&&vehicle_match(&store,"UNKNOWN")==-1);
    store.rows[1]=p;assert(!vehicle_store_valid(&store));store.rows[1]=(vehicle_profile_t){0};
    store.rows[0].efficiency=NAN;assert(!vehicle_store_valid(&store));store.rows[0]=p;
    float kwh=0;assert(vehicle_need(&p,20,80,&kwh)&&fabsf(kwh-40)<.001f);
    assert(!vehicle_need(&p,80,80,&kwh)&&!vehicle_need(&p,NAN,80,&kwh)&&!vehicle_need(&p,-1,80,&kwh));
    assert(fabs(vehicle_seconds(40,11)-13090.909)<.01);
    int64_t deadline=1793300000;assert(vehicle_grid_start(deadline,40,11)==deadline-14400);
    assert(vehicle_grid_start(deadline,1,11)==deadline-628);
    assert(!vehicle_grid_start(deadline,40,0));
    vehicle_profile_t optional=p;optional.cards[0][0]=optional.cards[1][0]=0;
    assert(vehicle_profile_valid(&optional));
    for(unsigned picture=0;picture<=5;picture++){optional.picture=picture;assert(vehicle_profile_valid(&optional));}
    optional.picture=6;assert(!vehicle_profile_valid(&optional));
    vehicle_session_t s;
    cJSON *t=cJSON_Parse("{\"privateId\":123,\"token\":\"abc123\",\"consumed\":110}");vehicle_session_parse(t,&s);cJSON_Delete(t);
    assert(s.present&&s.energy_valid&&s.consumed_wh==110&&!strcmp(s.card,"ABC123")&&!strcmp(s.identity,"123"));
    t=cJSON_Parse("{\"privateId\":\"tx123\",\"token\":{\"tokenCode\":\"second\"},\"consumed\":-1}");vehicle_session_parse(t,&s);cJSON_Delete(t);
    assert(s.present&&!s.energy_valid&&!strcmp(s.card,"SECOND")&&!strcmp(s.identity,"tx123"));
    vehicle_session_parse(NULL,&s);assert(!s.present&&!s.card[0]);
    t=cJSON_Parse("{\"name\":\"Auto\",\"plate\":\"A 1\",\"cards\":[\"aBc\",\"\"],\"capacity_kwh\":60,\"maximum_kw\":11,\"efficiency\":0.9,\"target_soc\":80,\"soc_source\":0}");
    assert(vehicle_profile_parse(t,&p)&&!strcmp(p.cards[0],"ABC"));cJSON_AddNumberToObject(t,"unexpected",1);assert(!vehicle_profile_parse(t,&p));cJSON_Delete(t);
    puts("PASS: card normalization/mapping, duplicate rejection, profile bounds, real transaction forms, energy units and deadline/loss calculations");
}
