static cJSON *gpio_snapshot(void){
    cJSON *root=cJSON_CreateObject(),*rows=cJSON_AddArrayToObject(root,"roles"),*pins=cJSON_AddArrayToObject(root,"pins");
    if(!root||!rows||!pins){cJSON_Delete(root);return NULL;}
    cJSON_AddStringToObject(root,"board",EMS_HARDWARE_NAME);
    LOCK();
    for(unsigned i=0;i<EMS_GPIO_ROLES;i++){
        cJSON *row=cJSON_CreateObject();if(!row){UNLOCK();cJSON_Delete(root);return NULL;}
        cJSON_AddStringToObject(row,"id",gpio_roles[i].id);cJSON_AddStringToObject(row,"label",gpio_roles[i].label);
        cJSON_AddStringToObject(row,"help",gpio_roles[i].help);cJSON_AddNumberToObject(row,"default",gpio_roles[i].standard);
        cJSON_AddNumberToObject(row,"value",settings_gpio_value(&saved_settings,i));
        cJSON_AddNumberToObject(row,"active",settings_gpio_value(&settings,i));
        cJSON_AddBoolToObject(row,"used",settings_gpio_used(&saved_settings,i));cJSON_AddBoolToObject(row,"led",i==11);
        cJSON_AddItemToArray(rows,row);
    }
    UNLOCK();
    for(int pin=0;pin<=48;pin++){
        cJSON *item=cJSON_CreateObject();if(!item){cJSON_Delete(root);return NULL;}
        cJSON_AddNumberToObject(item,"pin",pin);
        cJSON_AddStringToObject(item,"reason",settings_gpio_reason(pin,false));
        cJSON_AddBoolToObject(item,"led_allowed",!settings_gpio_reason(pin,true)[0]);cJSON_AddItemToArray(pins,item);
    }
    return root;
}
static bool gpio_stopped_locked(void){
    return !phase_test_active&&!boot_validation_pending&&!ota_in_progress&&!restarting&&!backup_in_progress&&!evcc_configuring&&
        !settings.enabled&&!(evcc_control.configured&&evcc_control.enabled)&&
        hardware_config_change_allowed(false,phase_config_quiescent_locked(),
            actual_at>0||meter_at>0,fresh(now_ms(),actual_at,wallbox_meter_ttl()),actual_a);
}
static esp_err_t gpio_post(httpd_req_t *req,cJSON *o){
    cJSON *changes=cJSON_GetObjectItemCaseSensitive(o,"values");
    settings_t *next=malloc(sizeof(*next));if(!next){cJSON_Delete(o);return httpd_resp_send_err(req,HTTPD_500_INTERNAL_SERVER_ERROR,"Zu wenig Speicher.");}
    LOCK();*next=saved_settings;bool stopped=gpio_stopped_locked();UNLOCK();
    bool valid=cJSON_IsObject(changes)&&changes->child;
    for(cJSON *item=valid?changes->child:NULL;item&&valid;item=item->next){
        unsigned role=0;while(role<EMS_GPIO_ROLES&&item->string&&strcmp(item->string,gpio_roles[role].id))role++;
        valid=item->string&&role<EMS_GPIO_ROLES&&cJSON_IsNumber(item)&&isfinite(item->valuedouble)&&
            item->valuedouble>=-1&&item->valuedouble<=48&&floor(item->valuedouble)==item->valuedouble;
        for(cJSON *other=item->next;other;other=other->next)if(other->string&&item->string&&!strcmp(item->string,other->string))valid=false;
        if(valid)settings_gpio_set(next,role,(int)item->valuedouble);
    }
    cJSON_Delete(o);
    if(!stopped){free(next);httpd_resp_set_status(req,"409 Conflict");return httpd_resp_sendstr(req,"Ladung zuerst stoppen. Nach vorhandenen Messungen frische Werte unter 1 A abwarten.");}
    if(!valid||!settings_gpio_valid(next)||!settings_valid(next)){free(next);return httpd_resp_send_err(req,HTTPD_400_BAD_REQUEST,"GPIO-Belegung ungueltig: reservierte Pins oder doppelte Belegung aktiver Funktionen.");}
    if(xSemaphoreTake(maintenance_flash_lock,ticks(2000))!=pdTRUE){free(next);httpd_resp_set_status(req,"409 Conflict");return httpd_resp_sendstr(req,"Wartung aktiv. Noch nicht gespeichert.");}
    LOCK();
    if(!gpio_stopped_locked()){UNLOCK();xSemaphoreGive(maintenance_flash_lock);free(next);httpd_resp_set_status(req,"409 Conflict");return httpd_resp_sendstr(req,"Ladung oder Wartung aktiv. Noch nicht gespeichert.");}
    bool changed=!settings_gpio_equal(next,&saved_settings),previous_pending=reboot_required;
    if(changed)reboot_required=true; /* Reserve against MQTT/evcc starts before committing. */
    UNLOCK();
    next->enabled=false;esp_err_t err=changed?save_config(next):ESP_OK;
    LOCK();
    if(err==ESP_OK&&changed){saved_settings=*next;settings.enabled=false;event_locked("gpio_saved");}
    else if(err!=ESP_OK)reboot_required=previous_pending;
    bool pending=restart_pending();UNLOCK();xSemaphoreGive(maintenance_flash_lock);free(next);
    if(err!=ESP_OK)return httpd_resp_send_err(req,HTTPD_500_INTERNAL_SERVER_ERROR,"GPIO-Belegung konnte nicht gespeichert werden.");
    cJSON *result=cJSON_CreateObject();cJSON_AddBoolToObject(result,"ok",true);cJSON_AddBoolToObject(result,"restart_needed",changed);
    cJSON_AddBoolToObject(result,"reboot_required",pending);return send_json(req,result);
}
