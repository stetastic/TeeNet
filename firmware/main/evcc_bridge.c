#include "evcc_bridge.h"
#include "cJSON.h"
#include <math.h>
#include <stddef.h>
#include <stdio.h>
#include <string.h>

static bool unique_keys(const cJSON *value) {
    if (cJSON_IsObject(value)) {
        for (const cJSON *a=value->child;a;a=a->next) {
            for (const cJSON *b=a->next;b;b=b->next)
                if (!strcmp(a->string,b->string)) return false;
        }
    }
    for (const cJSON *child=value->child;child;child=child->next)
        if (!unique_keys(child)) return false;
    return true;
}

static char shell_state(const char *value) {
    if (!value || !value[0]) return '?';
    if (!strcmp(value,"Available")) return 'A';
    if (!strcmp(value,"Preparing") || !strcmp(value,"SuspendedEV") ||
        !strcmp(value,"SuspendedEVSE")) return 'B';
    if (!strcmp(value,"Charging")) return 'C';
    if (!strcmp(value,"Finishing")) return 'D';
    if (!strcmp(value,"Faulted") || !strcmp(value,"ERROR") ||
        !strcmp(value,"Unavailable") || !strcmp(value,"OFF")) return 'F';
    return '?';
}

bool evcc_shell_parse(const char *body, evcc_shell_t *out) {
    if (!out) return false;
    *out=(evcc_shell_t){.state='?'};
    strcpy(out->reason,"invalid_json");
    if (!body || strlen(body)>6143) return false;
    cJSON *root=cJSON_ParseWithOpts(body,NULL,true);
    if (!cJSON_IsObject(root) || !unique_keys(root)) {cJSON_Delete(root);return false;}
    const cJSON *rows=cJSON_GetObjectItemCaseSensitive(root,"connectors"),*found=NULL;
    if (cJSON_IsArray(rows)) for (const cJSON *row=rows->child;row;row=row->next) {
        const cJSON *id=cJSON_GetObjectItemCaseSensitive(row,"id");
        if (cJSON_IsNumber(id) && id->valuedouble==1) {
            if (found) {cJSON_Delete(root);return false;}
            found=row;
        }
    }
    if (!found) {strcpy(out->reason,"connector_missing");cJSON_Delete(root);return false;}
    const cJSON *connector_status=cJSON_GetObjectItemCaseSensitive(found,"status");
    const cJSON *transaction=cJSON_GetObjectItemCaseSensitive(found,"transaction");
    const cJSON *status=cJSON_IsObject(transaction)?cJSON_GetObjectItemCaseSensitive(transaction,"statusCar"):NULL;
    const cJSON *session=cJSON_IsObject(transaction)?cJSON_GetObjectItemCaseSensitive(transaction,"status"):NULL;
    const char *raw=cJSON_IsString(status)&&status->valuestring[0]?status->valuestring:
        cJSON_IsString(connector_status)?connector_status->valuestring:"";
    snprintf(out->raw,sizeof(out->raw),"%s",raw);

    /* statusCar is the strongest signal. The connector state is a fallback for
       the moments before and after a transaction. An absent transaction is a
       useful display signal, but evcc_shell_fresh deliberately does not accept
       state A, so it can never enable charging from that inference. */
    if (cJSON_IsString(status) && status->valuestring[0]) {
        out->state=shell_state(status->valuestring);
        out->evcc_usable=!strcmp(status->valuestring,"Charging") ||
            !strcmp(status->valuestring,"SuspendedEV") ||
            !strcmp(status->valuestring,"SuspendedEVSE") ||
            !strcmp(status->valuestring,"Faulted");
        if (!strcmp(status->valuestring,"idle")) {
            if(cJSON_IsString(session)&&!strcmp(session->valuestring,"CHARGING")){out->state='B';out->evcc_usable=true;}
            else if(cJSON_IsString(session)&&!strcmp(session->valuestring,"FINISHED"))out->state='D';
        }
    }
    if(out->state=='?' && cJSON_IsString(connector_status))out->state=shell_state(connector_status->valuestring);
    if(!cJSON_IsObject(transaction) && out->state=='?')out->state='A';

    if(out->state!='?')strcpy(out->reason,"ok");
    else if(!cJSON_IsObject(transaction))strcpy(out->reason,"transaction_missing");
    else if(!cJSON_IsString(status)||!status->valuestring[0])strcpy(out->reason,"statuscar_missing");
    else strcpy(out->reason,"statuscar_unverified");
    cJSON_Delete(root);
    return true;
}

bool evcc_shell_fresh(const evcc_shell_t *sample,int64_t at,int64_t now) {
    return sample && sample->evcc_usable && at>0 && now>=at && now-at<EVCC_SHELL_TTL_MS &&
        (sample->state=='B' || sample->state=='C' || sample->state=='F');
}
bool shell_vehicle_fresh(const evcc_shell_t *sample,int64_t at,int64_t now) {
    return sample && at>0 && now>=at && now-at<EVCC_SHELL_TTL_MS &&
        (sample->state=='A' || sample->state=='B' || sample->state=='C' ||
         sample->state=='D' || sample->state=='F');
}
void evcc_configure(evcc_control_t *control,bool configured) {
    *control=(evcc_control_t){.configured=configured};
}
void evcc_touch(evcc_control_t *control,int64_t now) {
    if(control->configured) {evcc_tick(control,now);control->last_contact=now;}
}
bool evcc_set_current(evcc_control_t *control,float value,float minimum,float maximum,int64_t now) {
    if(!control->configured || !isfinite(value) || !isfinite(minimum) || !isfinite(maximum) ||
       minimum<=0 || maximum<minimum || value<minimum || value>maximum) return false;
    control->current_a=value;control->current_set=true;evcc_touch(control,now);
    return true; /* Setting a current must never implicitly start charging. */
}
bool evcc_set_enabled(evcc_control_t *control,bool enabled,bool ready,int64_t now) {
    if(!control->configured) return false;
    if(enabled && (!ready || !control->current_set || control->inhibited)) return false;
    control->enabled=enabled;control->preparing=false;control->expired=false;control->last_contact=now;return true;
}
bool evcc_set_phases(evcc_control_t *control,unsigned phases,bool supported,bool ready,int64_t now) {
    if(!control->configured || !supported || !ready || control->inhibited || (phases!=1 && phases!=3))return false;
    control->phases=(uint8_t)phases;control->phase_set=true;control->preparing=!control->enabled;
    control->expired=false;control->last_contact=now;return true;
}
unsigned evcc_desired_phases(const evcc_control_t *control,unsigned active) {
    if(!control->configured || control->inhibited || control->expired || (!control->enabled && !control->preparing))return 3;
    return control->phase_set?control->phases:active;
}
void evcc_local_stop(evcc_control_t *control) {
    control->enabled=false;control->preparing=false;control->inhibited=true;
}
bool evcc_local_release(evcc_control_t *control,int64_t now) {
    if(!control->configured || !control->inhibited)return false;
    control->inhibited=false;control->enabled=false;control->preparing=false;
    control->last_contact=now;
    return true; /* Requires a new evcc enable command; never starts locally. */
}
bool evcc_tick(evcc_control_t *control,int64_t now) {
    if(!control->configured || (!control->enabled && !control->preparing)) return false;
    if(control->last_contact<=0 || now<control->last_contact || now-control->last_contact>=EVCC_LEASE_MS) {
        control->enabled=false;control->preparing=false;control->expired=true;return true;
    }
    return false;
}
bool evcc_emergency_stop_json(const char *body) {
    cJSON *root=body?cJSON_ParseWithOpts(body,NULL,true):NULL;
    bool stop=false,ok=cJSON_IsObject(root)&&unique_keys(root)&&root->child;
    if(ok) for(const cJSON *i=root->child;i;i=i->next) {
        if(!strcmp(i->string,"enabled")&&cJSON_IsFalse(i)) stop=true;
        else if(!strcmp(i->string,"mode")&&cJSON_IsString(i)&&!strcmp(i->valuestring,"off")) stop=true;
        else {ok=false;break;}
    }
    cJSON_Delete(root);return ok&&stop;
}
bool evcc_host_valid(const char *host) {
    if(!host || !host[0] || strlen(host)>63 || host[0]=='.' || host[0]=='-' ||
       host[strlen(host)-1]=='.' || host[strlen(host)-1]=='-') return false;
    for(const char *p=host;*p;p++) {
        char c=*p;
        if(!((c>='a'&&c<='z')||(c>='A'&&c<='Z')||(c>='0'&&c<='9')||c=='.'||c=='-')) return false;
    }
    return true;
}
