/* Also guarded by the ordinary API token and the expert-session password. */
#include "shelly_phase.h"
static esp_err_t phase_mqtt_post(httpd_req_t *req,cJSON *o){
    cJSON *value=cJSON_GetObjectItemCaseSensitive(o,"enabled"),*repeat=cJSON_GetObjectItemCaseSensitive(o,"repeat_pin");
    cJSON *topic=cJSON_GetObjectItemCaseSensitive(o,"topic"),*polarity=cJSON_GetObjectItemCaseSensitive(o,"true_three_phase");
    cJSON *method=cJSON_GetObjectItemCaseSensitive(o,"transport"),*host=cJSON_GetObjectItemCaseSensitive(o,"host"),
        *channel=cJSON_GetObjectItemCaseSensitive(o,"switch_id"),*password=cJSON_GetObjectItemCaseSensitive(o,"password");
    unsigned transport=method&&cJSON_IsNumber(method)?method->valueint:0;
    unsigned switch_id=channel&&cJSON_IsNumber(channel)?channel->valueint:0;
    bool enabled=cJSON_IsTrue(value),valid=cJSON_IsBool(value);
    if(enabled)valid=valid&&cJSON_IsTrue(cJSON_GetObjectItemCaseSensitive(o,"confirm_risk"))&&
        cJSON_IsTrue(cJSON_GetObjectItemCaseSensitive(o,"confirm_no_feedback"))&&
        cJSON_IsString(repeat)&&expert_pin_valid(repeat->valuestring)&&
        cJSON_IsBool(polarity)&&(!method||(cJSON_IsNumber(method)&&method->valuedouble==transport&&transport<=3))&&
        (!password||(cJSON_IsString(password)&&strlen(password->valuestring)<65&&
          (transport==3||!password->valuestring[0])))&&
        (!channel||(cJSON_IsNumber(channel)&&channel->valuedouble==switch_id&&(switch_id==0||switch_id==100)))&&
        (transport==0?(cJSON_IsString(topic)&&strlen(topic->valuestring)<128):
         (cJSON_IsString(host)&&strlen(host->valuestring)<64&&
          shelly_phase_endpoint_valid(host->valuestring,strlen(host->valuestring)+1)));
    for(cJSON *i=o->child;i&&valid;i=i->next)
        for(cJSON *j=i->next;j;j=j->next)if(!strcmp(i->string,j->string))valid=false;
    if(!valid){cJSON_Delete(o);return httpd_resp_send_err(req,HTTPD_400_BAD_REQUEST,
        "Warnungen bestaetigen und Experten-Passwort erneut eingeben. Schnittstelle, Adresse und Schuetztyp pruefen.");}
    bool restart_needed=false;
    esp_err_t err=save_phase_mqtt_permission(enabled,enabled&&cJSON_IsString(topic)?topic->valuestring:NULL,
        cJSON_IsTrue(polarity),(uint8_t)transport,enabled&&cJSON_IsString(host)?host->valuestring:NULL,(uint8_t)switch_id,
        enabled&&cJSON_IsString(password)?password->valuestring:NULL,&restart_needed);cJSON_Delete(o);
    if(err==ESP_ERR_INVALID_STATE){httpd_resp_set_status(req,"409 Conflict");return httpd_resp_sendstr(req,
        "Ladung stoppen und ausstehenden Neustart durchfuehren. Zaehlertyp fuer mindestens L1/L2 und Schuetzverbindung pruefen. Bekannte Messwerte muessen frisch und unter 1 A sein.");}
    if(err!=ESP_OK){httpd_resp_set_status(req,"500 Internal Server Error");return httpd_resp_sendstr(req,
        "Freigabe nicht gespeichert. Abwahl sperrt weitere Ausgaben sofort; einen bereits begonnenen Versand kann sie nicht zurueckrufen. Speichern erneut versuchen.");}
    cJSON *result=cJSON_CreateObject();cJSON_AddBoolToObject(result,"ok",true);
    LOCK();bool pending=restart_pending();UNLOCK();
    cJSON_AddBoolToObject(result,"reboot_required",pending);cJSON_AddBoolToObject(result,"restart_needed",restart_needed);
    return send_json(req,result);
}
