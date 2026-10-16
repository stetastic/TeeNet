#include "shelly_phase.h"
#include "ems_core.h"
#include "cJSON.h"
#include <string.h>
#include <stdio.h>
#include <math.h>
#include <ctype.h>

static const cJSON *item(const cJSON *o,const char *key){
    const cJSON *found=NULL;
    for(const cJSON *p=o?o->child:NULL;p;p=p->next)if(p->string&&!strcmp(p->string,key)){
        if(found)return NULL;
        found=p;
    }
    return found;
}
static bool text(const cJSON *o,const char *key,char *dest,size_t capacity){
    const cJSON *v=item(o,key);
    if(!cJSON_IsString(v)||!v->valuestring[0]||strlen(v->valuestring)>=capacity)return false;
    for(const unsigned char *p=(const unsigned char *)v->valuestring;*p;p++)if(*p<32||*p==127)return false;
    strcpy(dest,v->valuestring);return true;
}
static bool boolean(const cJSON *o,const char *key,bool *value){
    const cJSON *v=item(o,key);if(!cJSON_IsBool(v))return false;
    *value=cJSON_IsTrue(v);return true;
}
static bool component_id(const cJSON *o,unsigned id){
    const cJSON *v=item(o,"id");return cJSON_IsObject(o)&&cJSON_IsNumber(v)&&v->valuedouble==id;
}
static bool no_errors(const cJSON *o){
    unsigned matches=0;
    for(const cJSON *p=o?o->child:NULL;p;p=p->next)if(p->string&&!strcmp(p->string,"errors"))matches++;
    const cJSON *v=item(o,"errors");
    return matches==0 || (matches==1&&cJSON_IsArray(v)&&cJSON_GetArraySize(v)==0);
}
bool shelly_phase_identity(const char *json,shelly_phase_identity_t *out){
    if(!json||!out)return false;
    cJSON *root=cJSON_ParseWithOpts(json,NULL,true);shelly_phase_identity_t next={.profile=255};
    const cJSON *gen=item(root,"gen");
    bool ok=cJSON_IsObject(root)&&text(root,"id",next.id,sizeof(next.id))&&
        !strncmp(next.id,"shelly",6)&&text(root,"model",next.model,sizeof(next.model))&&
        cJSON_IsNumber(gen)&&isfinite(gen->valuedouble)&&gen->valuedouble>=2&&gen->valuedouble<=4&&
        gen->valuedouble==gen->valueint&&boolean(root,"auth_en",&next.protected_device);
    if(ok){
        next.generation=(unsigned)gen->valueint;
        next.pro1=next.generation==2&&!strncmp(next.id,"shellypro1-",11);
        const char *names[]={"shellypro1-","shellypro2-","shellyplus1-","shelly1g3-","shelly1g4-"};
        const unsigned generations[]={2,2,2,3,4};
        for(unsigned i=0;i<5;i++)if(next.generation==generations[i]&&!strncmp(next.id,names[i],strlen(names[i]))){
            next.profile=(uint8_t)i;next.inputs=i<2?2:1;break;
        }
        if(!text(root,"name",next.name,sizeof(next.name)))snprintf(next.name,sizeof(next.name),"%s",next.id);
        *out=next;
    }
    cJSON_Delete(root);return ok;
}
bool shelly_phase_feedback_for(const char *json,shelly_phase_feedback_t *out,unsigned count,bool closed_is_single){
    if(!json||!out||count<1||count>2)return false;
    cJSON *root=cJSON_ParseWithOpts(json,NULL,true);shelly_phase_feedback_t next={0};
    const cJSON *sw=item(root,"switch:0"),*no=item(root,"input:0"),*nc=item(root,"input:1"),*sys=item(root,"sys");
    const cJSON *up=item(sys,"uptime");
    bool ok=cJSON_IsObject(root)&&component_id(sw,0)&&component_id(no,0)&&
        boolean(sw,"output",&next.output)&&boolean(no,"state",&next.no_closed)&&
        (count==1||(component_id(nc,1)&&boolean(nc,"state",&next.nc_closed)&&no_errors(nc)))&&
        cJSON_IsNumber(up)&&isfinite(up->valuedouble)&&up->valuedouble>=0&&up->valuedouble<=UINT32_MAX&&floor(up->valuedouble)==up->valuedouble&&
        no_errors(sw)&&no_errors(no);
    if(ok){
        next.phases=phase_feedback_contacts(next.no_closed,next.nc_closed,count,closed_is_single);
        next.uptime=(uint32_t)up->valuedouble;next.has_uptime=true;*out=next;
    }
    cJSON_Delete(root);return ok;
}
bool shelly_phase_feedback(const char *json,shelly_phase_feedback_t *out){return shelly_phase_feedback_for(json,out,2,false);}
bool shelly_phase_config_for(const char *json,unsigned count,bool pulse){
    if(!json||count<1||count>2||(pulse&&count!=2))return false;
    cJSON *root=cJSON_ParseWithOpts(json,NULL,true);
    const cJSON *sw=item(root,"switch:0"),*no=item(root,"input:0"),*nc=item(root,"input:1");
    const cJSON *mode=item(sw,"in_mode"),*initial=item(sw,"initial_state"),*delay=item(sw,"auto_off_delay");
    bool on=false,off=false,enabled=false,inverted=false;
    bool ok=component_id(sw,0)&&component_id(no,0)&&(count==1||component_id(nc,1))&&
        cJSON_IsString(mode)&&!strcmp(mode->valuestring,"detached")&&
        cJSON_IsString(initial)&&!strcmp(initial->valuestring,"off")&&
        boolean(sw,"auto_on",&on)&&!on&&boolean(sw,"auto_off",&off)&&off==pulse&&
        (!pulse||(cJSON_IsNumber(delay)&&delay->valuedouble>=0.5&&delay->valuedouble<=1.0));
    const cJSON *inputs[]={no,nc};
    for(unsigned i=0;i<count&&ok;i++){
        const cJSON *type=item(inputs[i],"type");
        ok=boolean(inputs[i],"enable",&enabled)&&enabled&&boolean(inputs[i],"invert",&inverted)&&!inverted&&
            cJSON_IsString(type)&&!strcmp(type->valuestring,"switch");
    }
    cJSON_Delete(root);return ok;
}
bool shelly_phase_safe_config(const char *json){return shelly_phase_config_for(json,2,true);}
bool shelly_phase_endpoint_valid(const char *value,size_t capacity){
    if(!value||!memchr(value,0,capacity)||!value[0])return false;
    /* A host only: reject schemes, ports, credentials, paths and query injection. */
    if(value[0]=='-'||value[0]=='.')return false;
    for(const unsigned char *p=(const unsigned char *)value;*p;p++)if(!isalnum(*p)&&*p!='.'&&*p!='-')return false;
    return true;
}
bool shelly_phase_pulse_path(char *out,size_t capacity){
    if(!out)return false;
    int length=snprintf(out,capacity,"/rpc/Switch.Set?id=0&on=true&toggle_after=0.5");
    return length>0&&(size_t)length<capacity;
}
bool shelly_phase_pulse_ack(const char *json){
    if(!json)return false;
    cJSON *root=cJSON_ParseWithOpts(json,NULL,true);
    bool previous=true;
    bool ok=cJSON_IsObject(root)&&boolean(root,"was_on",&previous)&&!previous;
    cJSON_Delete(root);return ok;
}
bool shelly_output_config(const char *json,unsigned id){
    cJSON *root=cJSON_ParseWithOpts(json,NULL,true);
    const cJSON *mode=item(root,"in_mode"),*initial=item(root,"initial_state");bool a=true,b=true;
    bool ok=component_id(root,id)&&cJSON_IsString(mode)&&!strcmp(mode->valuestring,"detached")&&
        cJSON_IsString(initial)&&!strcmp(initial->valuestring,"off")&&
        boolean(root,"auto_on",&a)&&!a&&boolean(root,"auto_off",&b)&&!b;
    cJSON_Delete(root);return ok;
}
bool shelly_output_status(const char *json,unsigned id,bool *on){
    cJSON *root=cJSON_ParseWithOpts(json,NULL,true);bool value=false;
    bool ok=component_id(root,id)&&boolean(root,"output",&value)&&no_errors(root);
    if(ok&&on)*on=value;
    cJSON_Delete(root);return ok;
}
bool shelly_output_ack(const char *json){
    cJSON *root=cJSON_ParseWithOpts(json,NULL,true);bool previous=false;
    bool ok=cJSON_IsObject(root)&&boolean(root,"was_on",&previous);cJSON_Delete(root);return ok;
}
bool shelly_output_path(char *out,size_t capacity,const char *method,unsigned id,bool on){
    if(!out||!method||(id!=0&&id!=100))return false;
    if(strcmp(method,"Set")&&strcmp(method,"GetStatus")&&strcmp(method,"GetConfig"))return false;
    int n=!strcmp(method,"Set")?snprintf(out,capacity,"/rpc/Switch.Set?id=%u&on=%s",id,on?"true":"false"):
        snprintf(out,capacity,"/rpc/Switch.%s?id=%u",method,id);
    return n>0&&(size_t)n<capacity;
}
void shelly_output_coil_request(uint8_t out[12],uint16_t tx,unsigned id,bool writing,bool on){
    uint16_t coil=(uint16_t)(100+id*10);
    uint8_t frame[12]={tx>>8,tx&255,0,0,0,6,1,writing?5:1,coil>>8,coil&255,writing&&on?255:0,writing?0:1};
    memcpy(out,frame,sizeof(frame));
}
bool shelly_output_coil_reply(const uint8_t *reply,size_t size,const uint8_t request[12],bool *on){
    if(!reply||!request||size<9||memcmp(reply,request,4)||reply[6]!=1)return false;
    if(request[7]==5)return size==12&&!memcmp(reply,request,12);
    if(request[7]!=1||size!=10||reply[4]!=0||reply[5]!=4||reply[7]!=1||reply[8]!=1||reply[9]>1)return false;
    if(on)*on=reply[9]==1;
    return true;
}
