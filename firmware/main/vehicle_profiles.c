#include "vehicle_profiles.h"
#include <math.h>
#include <stdio.h>
#include <string.h>
bool vehicle_estimated_soc(float start,float capacity,float efficiency,double anchor_wh,double total_wh,float *soc){
    if(!soc||!isfinite(start)||start<0||start>100||!isfinite(capacity)||capacity<5||capacity>200||
       !isfinite(efficiency)||efficiency<.5f||efficiency>1||!isfinite(anchor_wh)||!isfinite(total_wh)||
       anchor_wh<0||total_wh<anchor_wh)return false;
    *soc=fminf(100,start+(float)((total_wh-anchor_wh)/1000/capacity*efficiency*100));return true;
}
bool vehicle_card(const char *text,char out[VEHICLE_CARD_BYTES]){
    out[0]=0;if(!text)return false;
    while(*text==' ')text++;
    size_t n=strlen(text);while(n&&text[n-1]==' ')n--;
    if(!n||n>=VEHICLE_CARD_BYTES)return false;
    for(size_t i=0;i<n;i++){unsigned char c=(unsigned char)text[i];if(c<33||c>126)return false;out[i]=(char)(c>='a'&&c<='z'?c-32:c);}
    out[n]=0;return true;
}
static bool bounded_text(const char *s,size_t n){
    for(size_t i=0;i<n;i++){if(!s[i])return true;if((unsigned char)s[i]<32)return false;}
    return false;
}
bool vehicle_profile_valid(const vehicle_profile_t *p){
    if(!p||!bounded_text(p->name,sizeof(p->name))||!bounded_text(p->plate,sizeof(p->plate)))return false;
    for(unsigned i=0;i<2;i++){
        if(!bounded_text(p->cards[i],sizeof(p->cards[i])))return false;
        char normalized[VEHICLE_CARD_BYTES];if(p->cards[i][0]&&(!vehicle_card(p->cards[i],normalized)||strcmp(normalized,p->cards[i])))return false;
    }
    if(p->picture>5)return false;
    if(!p->name[0])return !p->picture&&!p->soc_source&&!p->plate[0]&&!p->cards[0][0]&&!p->cards[1][0]&&p->capacity_kwh==0&&p->maximum_kw==0&&p->efficiency==0&&p->target_soc==0;
    return isfinite(p->capacity_kwh)&&p->capacity_kwh>=5&&p->capacity_kwh<=200&&
        isfinite(p->maximum_kw)&&p->maximum_kw>=1.4f&&p->maximum_kw<=22&&
        isfinite(p->efficiency)&&p->efficiency>=.5f&&p->efficiency<=1&&
        isfinite(p->target_soc)&&p->target_soc>=1&&p->target_soc<=100&&p->soc_source<=2;
}
bool vehicle_store_valid(const vehicle_store_t *s){
    if(!s||s->version!=1)return false;
    for(unsigned i=0;i<VEHICLE_COUNT;i++){
        if(!vehicle_profile_valid(&s->rows[i]))return false;
        if(s->rows[i].name[0]&&s->rows[i].soc_source==2)for(unsigned j=0;j<i;j++)if(s->rows[j].name[0]&&s->rows[j].soc_source==2)return false;
        for(unsigned a=0;a<2;a++)if(s->rows[i].cards[a][0])
            for(unsigned j=0;j<=i;j++)for(unsigned b=0;b<2;b++)
                if((j<i||b<a)&&!strcmp(s->rows[i].cards[a],s->rows[j].cards[b]))return false;
    }
    return true;
}
int vehicle_match(const vehicle_store_t *s,const char *card){
    char normalized[VEHICLE_CARD_BYTES];if(!s||!s->enabled||!vehicle_card(card,normalized))return -1;
    for(unsigned i=0;i<VEHICLE_COUNT;i++)if(s->rows[i].name[0])for(unsigned j=0;j<2;j++)
        if(!strcmp(s->rows[i].cards[j],normalized))return (int)i;
    return -1;
}
static bool copy_field(const cJSON *o,const char *key,char *out,size_t n){
    const cJSON *v=cJSON_GetObjectItemCaseSensitive(o,key);
    if(!cJSON_IsString(v)||strlen(v->valuestring)>=n)return false;
    strcpy(out,v->valuestring);return bounded_text(out,n);
}
static bool number_field(const cJSON *o,const char *key,float *out){
    const cJSON *v=cJSON_GetObjectItemCaseSensitive(o,key);if(!cJSON_IsNumber(v)||!isfinite(v->valuedouble))return false;
    *out=(float)v->valuedouble;return true;
}
bool vehicle_profile_parse(const cJSON *o,vehicle_profile_t *p){
    memset(p,0,sizeof(*p));if(!cJSON_IsObject(o))return false;
    const char *keys[]={"name","plate","cards","capacity_kwh","maximum_kw","efficiency","target_soc","soc_source","picture"};
    for(const cJSON *a=o->child;a;a=a->next){bool known=false;for(unsigned k=0;k<9;k++)if(!strcmp(a->string,keys[k]))known=true;if(!known)return false;
        for(const cJSON *b=a->next;b;b=b->next)if(!strcmp(a->string,b->string))return false;}
    const cJSON *source=cJSON_GetObjectItemCaseSensitive(o,"soc_source");
    if(!cJSON_IsNumber(source)||source->valuedouble<0||source->valuedouble>2||source->valuedouble!=source->valueint)return false;
    p->soc_source=(uint8_t)source->valueint;
    const cJSON *picture=cJSON_GetObjectItemCaseSensitive(o,"picture");
    if(picture&&(!cJSON_IsNumber(picture)||picture->valuedouble<0||picture->valuedouble>5||picture->valuedouble!=picture->valueint))return false;
    p->picture=picture?(uint8_t)picture->valueint:0;
    if(cJSON_GetArraySize(o)!=(picture?9:8)||!copy_field(o,"name",p->name,sizeof(p->name))||!copy_field(o,"plate",p->plate,sizeof(p->plate)))return false;
    const cJSON *cards=cJSON_GetObjectItemCaseSensitive(o,"cards");if(!cJSON_IsArray(cards)||cJSON_GetArraySize(cards)!=2)return false;
    for(unsigned i=0;i<2;i++){const cJSON *c=cJSON_GetArrayItem(cards,i);if(!cJSON_IsString(c))return false;if(c->valuestring[0]&&!vehicle_card(c->valuestring,p->cards[i]))return false;}
    return number_field(o,"capacity_kwh",&p->capacity_kwh)&&number_field(o,"maximum_kw",&p->maximum_kw)&&
        number_field(o,"efficiency",&p->efficiency)&&number_field(o,"target_soc",&p->target_soc)&&vehicle_profile_valid(p);
}
void vehicle_session_parse(const cJSON *t,vehicle_session_t *out){
    memset(out,0,sizeof(*out));if(!cJSON_IsObject(t))return;out->present=true;
    const cJSON *token=cJSON_GetObjectItemCaseSensitive(t,"token");
    if(cJSON_IsObject(token))token=cJSON_GetObjectItemCaseSensitive(token,"tokenCode");
    if(cJSON_IsString(token))vehicle_card(token->valuestring,out->card);
    const cJSON *id=cJSON_GetObjectItemCaseSensitive(t,"privateId");
    if(!id)id=cJSON_GetObjectItemCaseSensitive(t,"privateID");
    if(cJSON_IsString(id)&&strlen(id->valuestring)<sizeof(out->identity)&&bounded_text(id->valuestring,sizeof(out->identity)))strcpy(out->identity,id->valuestring);
    else if(cJSON_IsNumber(id)&&isfinite(id->valuedouble)&&id->valuedouble>=0&&id->valuedouble<=9007199254740991.0&&floor(id->valuedouble)==id->valuedouble)snprintf(out->identity,sizeof(out->identity),"%.0f",id->valuedouble);
    /* Identity must never be guessed from another connector or historic entry. */
    const cJSON *energy=cJSON_GetObjectItemCaseSensitive(t,"consumed");
    if(cJSON_IsNumber(energy)&&isfinite(energy->valuedouble)&&energy->valuedouble>=0&&energy->valuedouble<=1000000){out->energy_valid=true;out->consumed_wh=(float)energy->valuedouble;}
}
bool vehicle_need(const vehicle_profile_t *p,float soc,float target,float *kwh){
    if(!vehicle_profile_valid(p)||!p->name[0]||!isfinite(soc)||!isfinite(target)||soc<0||soc>100||target<=soc||target>100)return false;
    *kwh=p->capacity_kwh*(target-soc)/100/p->efficiency;return isfinite(*kwh)&&*kwh>0&&*kwh<=400;
}
double vehicle_seconds(float kwh,float kw){return isfinite(kwh)&&kwh>=0&&isfinite(kw)&&kw>0?(double)kwh/kw*3600:NAN;}
int64_t vehicle_grid_start(int64_t deadline,float kwh,float kw){
    double seconds=vehicle_seconds(kwh,kw);return isfinite(seconds)?deadline-(int64_t)ceil(seconds+fmax(300,seconds*.10)):0;
}
