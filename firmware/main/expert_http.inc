#include "json_owned.h"
/* Expert values are immutable during operation and activated only at boot. */
static int64_t expert_wrong_until;
static void expert_load(void) {
    expert_defaults(&expert_saved);
    nvs_handle_t nvs;
    if(config_storage_ok && nvs_open("expert",NVS_READONLY,&nvs)==ESP_OK){
        expert_values_t blob;size_t size=sizeof(blob);
        if(nvs_get_blob(nvs,"values",&blob,&size)==ESP_OK &&
           !expert_decode(&blob,size,&expert_saved))ESP_LOGW(TAG,"Expert values rejected; defaults loaded");
        /* Retired GPIO-input permission is intentionally not migrated. */
        nvs_close(nvs);
    }
    if(expert_saved.values[EP_MIN_CURRENT]>settings.max_charge_a){
        expert_saved.values[EP_MIN_CURRENT]=expert_meta[EP_MIN_CURRENT].standard;
        ESP_LOGW(TAG,"Expert minimum exceeds charger maximum; standard restored");
    }
    phase_mqtt_allowed=settings.phase_mqtt_output_enabled;
    expert_activate(&expert_saved);
    settings.min_charge_a=ems_param(EP_MIN_CURRENT);saved_settings.min_charge_a=settings.min_charge_a;
}
#include "gpio_http.inc"
/* Keep the authenticated response deliberately small.  Labels, help text and
   limits are served as a compressed static asset, so the ESP never has to
   allocate the complete expert screen as one large cJSON tree. */
static cJSON *expert_values_snapshot(void){
    cJSON *root=cJSON_CreateObject();if(!root)return NULL;
    cJSON *values=cJSON_AddArrayToObject(root,"values"),*active=cJSON_AddArrayToObject(root,"active");
    if(!values||!active){cJSON_Delete(root);return NULL;}
    for(unsigned p=0;p<EP_COUNT;p++){
        json_add_owned_to_array(values,cJSON_CreateNumber(expert_saved.values[p]));
        json_add_owned_to_array(active,cJSON_CreateNumber(ems_param(p)));
    }
    bool pending=false;for(unsigned p=0;p<EP_COUNT;p++)pending|=fabsf(expert_saved.values[p]-ems_param(p))>.00001f;
    cJSON_AddBoolToObject(root,"pending",pending);
    return root;
}
#include "expert_storage.inc"
#include "phase_mqtt_storage.inc"
#include "phase_mqtt_http.inc"
static esp_err_t expert_post(httpd_req_t *req){
    if(!authorized(req))return ESP_OK;
    cJSON *o=read_json(req);if(!o)return ESP_OK;
    cJSON *pin=cJSON_GetObjectItemCaseSensitive(o,"pin"),*action=cJSON_GetObjectItemCaseSensitive(o,"action");
    /* Authenticated local users may always revoke; enabling still needs both
       password checks and warnings. A duplicate enabled field is rejected by
       phase_mqtt_post before any permission is changed. */
    if(cJSON_IsString(action)&&!strcmp(action->valuestring,"mqtt_phase_output")&&
       cJSON_IsFalse(cJSON_GetObjectItemCaseSensitive(o,"enabled")))return phase_mqtt_post(req,o);
    if(now_ms()<expert_wrong_until){cJSON_Delete(o);httpd_resp_set_status(req,"429 Too Many Requests");return httpd_resp_sendstr(req,"Kurz warten und erneut versuchen.");}
    if(!cJSON_IsString(pin)||!expert_pin_valid(pin->valuestring)){
        expert_wrong_until=now_ms()+2000;cJSON_Delete(o);
        return httpd_resp_send_err(req,HTTPD_403_FORBIDDEN,"Experten-Passwort falsch.");
    }
    if(!cJSON_IsString(action)){cJSON_Delete(o);return httpd_resp_send_err(req,HTTPD_400_BAD_REQUEST,"Aktion fehlt.");}
    /* The protected phase setup only needs to verify the repeated PIN.  Do
       not allocate the large parameter/GPIO snapshot a second time. */
    if(!strcmp(action->valuestring,"authorize")){cJSON_Delete(o);return ok_json(req);}
    if(!strcmp(action->valuestring,"read")){cJSON_Delete(o);return send_json(req,expert_values_snapshot());}
    if(!strcmp(action->valuestring,"read_gpio")){cJSON_Delete(o);return send_json(req,gpio_snapshot());}
    if(boot_validation_pending){cJSON_Delete(o);httpd_resp_set_status(req,"409 Conflict");return httpd_resp_sendstr(req,"Firmware wird geprueft. Danach erneut versuchen.");}
    if(!strcmp(action->valuestring,"mqtt_phase_output"))return phase_mqtt_post(req,o);
    if(!strcmp(action->valuestring,"gpio"))return gpio_post(req,o);
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
