/* HTTP handlers share the application's validated configuration and state lock. */
static esp_err_t config_get(httpd_req_t *req){return send_json(req,config_json(false));}
static esp_err_t config_post(httpd_req_t *req) {
    if(!authorized(req))return ESP_OK;
    cJSON *o=read_json(req);return o?config_apply(req,o,false):ESP_OK;
}
static esp_err_t backup_handler(httpd_req_t *req) {
    if(!authorized(req))return ESP_OK;
    char query[32];bool secrets=httpd_req_get_url_query_str(req,query,sizeof(query))==ESP_OK&&!strcmp(query,"secrets=1");
    cJSON *root=cJSON_CreateObject(),*config=config_json(secrets);
    if(!root||!config){cJSON_Delete(root);cJSON_Delete(config);return send_json(req,NULL);}
    config_remove_readonly(config);
    cJSON_AddStringToObject(root,"format","TeeNet-settings");cJSON_AddNumberToObject(root,"schema",1);
    cJSON_AddStringToObject(root,"build_id",EMS_BUILD_ID);cJSON_AddBoolToObject(root,"includes_passwords",secrets);
    cJSON_AddItemToObject(root,"settings",config);
    httpd_resp_set_hdr(req,"Content-Disposition","attachment; filename=\"TeeNet-Einstellungen.json\"");
    return send_json(req,root);
}
static esp_err_t restore_handler(httpd_req_t *req) {
    if(!authorized(req))return ESP_OK;
    cJSON *root=read_json(req);if(!root)return ESP_OK;
    cJSON *format=cJSON_GetObjectItem(root,"format"),*schema=cJSON_GetObjectItem(root,"schema");
    cJSON *config=cJSON_DetachItemFromObject(root,"settings");
    bool valid=cJSON_IsString(format)&&!strcmp(format->valuestring,"TeeNet-settings")&&
        cJSON_IsNumber(schema)&&schema->valuedouble==1&&cJSON_IsObject(config)&&config->child;
    cJSON_Delete(root);
    if(valid)for(cJSON *i=config->child;i;i=i->next)for(cJSON *j=i->next;j;j=j->next)if(!strcmp(i->string,j->string))valid=false;
    if(!valid){cJSON_Delete(config);return httpd_resp_send_err(req,HTTPD_400_BAD_REQUEST,"Keine passende TeeNet-Einstellungssicherung.");}
    return config_apply(req,config,true);
}
#include "diagnostic_http.inc"
#include "plan_http.inc"
