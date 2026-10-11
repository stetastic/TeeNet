/* Expert values are immutable during operation and activated only at boot. */
static int64_t expert_wrong_until;
static void expert_load(void) {
    expert_defaults(&expert_saved);
    nvs_handle_t nvs;
    if(config_storage_ok && nvs_open("expert",NVS_READONLY,&nvs)==ESP_OK){
        expert_values_t blob;size_t size=sizeof(blob);
        if(nvs_get_blob(nvs,"values",&blob,&size)==ESP_OK &&
           !expert_decode(&blob,size,&expert_saved))ESP_LOGW(TAG,"Expert values rejected; defaults loaded");
        nvs_close(nvs);
    }
    if(expert_saved.values[EP_MIN_CURRENT]>settings.max_charge_a){
        expert_saved.values[EP_MIN_CURRENT]=expert_meta[EP_MIN_CURRENT].standard;
        ESP_LOGW(TAG,"Expert minimum exceeds charger maximum; standard restored");
    }
    expert_activate(&expert_saved);
    settings.min_charge_a=ems_param(EP_MIN_CURRENT);saved_settings.min_charge_a=settings.min_charge_a;
}
static cJSON *expert_snapshot(void){
    cJSON *root=cJSON_CreateObject();if(!root)return NULL;
    cJSON *array=cJSON_AddArrayToObject(root,"parameters");
    if(!array){cJSON_Delete(root);return NULL;}
    for(unsigned p=0;p<EP_COUNT;p++){
        const expert_meta_t *m=&expert_meta[p];cJSON *row=cJSON_CreateObject();
        cJSON_AddStringToObject(row,"id",m->id);cJSON_AddStringToObject(row,"group",m->group);
        cJSON_AddStringToObject(row,"label",m->label);cJSON_AddStringToObject(row,"help",m->help);
        cJSON_AddStringToObject(row,"unit",m->unit);
        cJSON_AddNumberToObject(row,"default",m->standard);cJSON_AddNumberToObject(row,"min",m->minimum);
        cJSON_AddNumberToObject(row,"max",m->maximum);cJSON_AddNumberToObject(row,"step",m->step);
        cJSON_AddNumberToObject(row,"value",expert_saved.values[p]);
        cJSON_AddNumberToObject(row,"active",ems_param(p));cJSON_AddItemToArray(array,row);
    }
    settings_t defaults;settings_defaults(&defaults);
    cJSON *config=cJSON_AddObjectToObject(root,"defaults");
    for(unsigned f=0;f<sizeof(fields)/sizeof(fields[0]);f++){
        const field_t *d=&fields[f];const void *ptr=(const char *)&defaults+d->offset;
        if(d->type=='p')continue;
        if(d->type=='s')cJSON_AddStringToObject(config,d->name,ptr);
        else if(d->type=='b')cJSON_AddBoolToObject(config,d->name,*(const bool *)ptr);
        else cJSON_AddNumberToObject(config,d->name,d->type=='f'?*(const float *)ptr:d->type=='i'?*(const int *)ptr:*(const uint8_t *)ptr);
    }
    cJSON_AddItemToObject(root,"settings",config_json(false));
    bool pending=false;for(unsigned p=0;p<EP_COUNT;p++)pending|=fabsf(expert_saved.values[p]-ems_param(p))>.00001f;
    cJSON_AddBoolToObject(root,"pending",pending);
    return root;
}
#include "expert_storage.inc"
static esp_err_t expert_post(httpd_req_t *req){
    if(!authorized(req))return ESP_OK;
    if(now_ms()<expert_wrong_until){httpd_resp_set_status(req,"429 Too Many Requests");return httpd_resp_sendstr(req,"Kurz warten und erneut versuchen.");}
    cJSON *o=read_json(req);if(!o)return ESP_OK;
    cJSON *pin=cJSON_GetObjectItemCaseSensitive(o,"pin"),*action=cJSON_GetObjectItemCaseSensitive(o,"action");
    if(!cJSON_IsString(pin)||!expert_pin_valid(pin->valuestring)){
        expert_wrong_until=now_ms()+2000;cJSON_Delete(o);
        return httpd_resp_send_err(req,HTTPD_403_FORBIDDEN,"Experten-Passwort falsch.");
    }
    if(!cJSON_IsString(action)){cJSON_Delete(o);return httpd_resp_send_err(req,HTTPD_400_BAD_REQUEST,"Aktion fehlt.");}
    if(!strcmp(action->valuestring,"read")){cJSON_Delete(o);return send_json(req,expert_snapshot());}
    LOCK();bool stopped=expert_stopped_locked();UNLOCK();
    if(!stopped){cJSON_Delete(o);httpd_resp_set_status(req,"409 Conflict");return httpd_resp_sendstr(req,"Ladung zuerst stoppen; frische Messwerte unter 1 A erforderlich.");}
    if(!strcmp(action->valuestring,"config")){
        cJSON *changes=cJSON_DetachItemFromObjectCaseSensitive(o,"changes");cJSON_Delete(o);
        if(!cJSON_IsObject(changes)){cJSON_Delete(changes);return httpd_resp_send_err(req,HTTPD_400_BAD_REQUEST,"Einstellungen fehlen.");}
        return config_apply(req,changes,false);
    }
    expert_values_t next=expert_saved;
    if(!strcmp(action->valuestring,"reset"))expert_defaults(&next);
    else if(!strcmp(action->valuestring,"save")){
        cJSON *values=cJSON_GetObjectItemCaseSensitive(o,"values");bool valid=cJSON_IsObject(values);
        for(cJSON *i=valid?values->child:NULL;i && valid;i=i->next){
            unsigned p=0;while(p<EP_COUNT && strcmp(i->string,expert_meta[p].id))p++;
            valid=p<EP_COUNT && cJSON_IsNumber(i) && isfinite(i->valuedouble);
            for(cJSON *j=i->next;j;j=j->next)if(!strcmp(i->string,j->string))valid=false;
            if(valid)next.values[p]=(float)i->valuedouble;
        }
        if(!valid){cJSON_Delete(o);return httpd_resp_send_err(req,HTTPD_400_BAD_REQUEST,"Unbekannter oder ungueltiger Regelparameter.");}
    }else {cJSON_Delete(o);return httpd_resp_send_err(req,HTTPD_400_BAD_REQUEST,"Aktion ungueltig.");}
    cJSON_Delete(o);
    if(!expert_valid(&next) || next.values[EP_MIN_CURRENT]>saved_settings.max_charge_a)
        return httpd_resp_send_err(req,HTTPD_400_BAD_REQUEST,"Grenzen pruefen: Mindeststrom, Stillstandszeit, Messwartezeit und evcc-Frist muessen zueinander passen.");
    esp_err_t err=save_expert_values(&next);
    if(err==ESP_ERR_INVALID_STATE || err==ESP_ERR_TIMEOUT){
        httpd_resp_set_status(req,"409 Conflict");return httpd_resp_sendstr(req,"Ladung oder Wartung aktiv. Regelparameter noch nicht gespeichert.");
    }
    if(err!=ESP_OK)return httpd_resp_send_err(req,HTTPD_500_INTERNAL_SERVER_ERROR,"Regelparameter konnten nicht gespeichert werden.");
    cJSON *result=cJSON_CreateObject();cJSON_AddBoolToObject(result,"ok",true);
    LOCK();bool pending=expert_reboot_pending,needs_reboot=restart_pending();UNLOCK();
    cJSON_AddBoolToObject(result,"restart_needed",pending);cJSON_AddBoolToObject(result,"reboot_required",needs_reboot);
    return send_json(req,result);
}
