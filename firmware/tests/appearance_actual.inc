static esp_err_t theme_get(httpd_req_t *req) {
    uint8_t selected=0,view=0,density=0,graphic_seen=0;
    nvs_handle_t nvs;
    if(nvs_open("wallbox_ems",NVS_READONLY,&nvs)==ESP_OK) {
        if(nvs_get_u8(nvs,"ui_theme",&selected)!=ESP_OK) selected=0;
        nvs_get_u8(nvs,"ui_view",&view);
        nvs_get_u8(nvs,"ui_density",&density);
        nvs_get_u8(nvs,"ui_graph_seen",&graphic_seen);
        nvs_close(nvs);
    }
    cJSON *o=cJSON_CreateObject();if(!o)return httpd_resp_send_err(req,HTTPD_500_INTERNAL_SERVER_ERROR,"Out of memory");
    cJSON_AddStringToObject(o,"theme",selected==2?"dark":selected==1?"light":"unset");
    cJSON_AddStringToObject(o,"view",view==1?"graphic":"standard");
    cJSON_AddStringToObject(o,"density",density==1?"compact":"standard");
    cJSON_AddBoolToObject(o,"graphic_notice_pending",graphic_seen<1);
    return send_json(req,o);
}

static esp_err_t theme_post(httpd_req_t *req) {
    if(!authorized(req)) return ESP_OK;
    cJSON *o=read_json(req);if(!o)return ESP_OK;
    cJSON *theme=cJSON_GetObjectItemCaseSensitive(o,"theme");
    cJSON *view=cJSON_GetObjectItemCaseSensitive(o,"view");
    cJSON *density=cJSON_GetObjectItemCaseSensitive(o,"density");
    cJSON *ack=cJSON_GetObjectItemCaseSensitive(o,"graphic_notice_ack");
    bool only=cJSON_IsObject(o) && cJSON_GetArraySize(o)==1;
    uint8_t selected=only&&cJSON_IsString(theme)&&!strcmp(theme->valuestring,"dark")?2:only&&cJSON_IsString(theme)&&!strcmp(theme->valuestring,"light")?1:0;
    bool has_view=only&&cJSON_IsString(view)&&(!strcmp(view->valuestring,"graphic")||!strcmp(view->valuestring,"standard"));
    bool has_density=only&&cJSON_IsString(density)&&(!strcmp(density->valuestring,"compact")||!strcmp(density->valuestring,"standard"));
    uint8_t compact=has_density&&!strcmp(density->valuestring,"compact")?1:0;
    bool has_ack=only&&cJSON_IsTrue(ack);
    uint8_t layout=has_view&&!strcmp(view->valuestring,"graphic")?1:0;
    cJSON_Delete(o);
    if(!selected&&!has_view&&!has_density&&!has_ack)return httpd_resp_send_err(req,HTTPD_400_BAD_REQUEST,"Ungueltige Darstellung.");
    nvs_handle_t nvs;esp_err_t err=nvs_open("wallbox_ems",NVS_READWRITE,&nvs);
    if(err==ESP_OK){err=has_ack?nvs_set_u8(nvs,"ui_graph_seen",1):has_density?nvs_set_u8(nvs,"ui_density",compact):has_view?nvs_set_u8(nvs,"ui_view",layout):nvs_set_u8(nvs,"ui_theme",selected);if(err==ESP_OK)err=nvs_commit(nvs);nvs_close(nvs);}
    if(err!=ESP_OK)return httpd_resp_send_err(req,HTTPD_500_INTERNAL_SERVER_ERROR,"Darstellung konnte nicht gespeichert werden.");
    return theme_get(req);
}

