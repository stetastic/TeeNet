static void load_settings(void) {

    settings_defaults(&settings); nvs_handle_t nvs;

    if(config_storage_ok && nvs_open("wallbox_ems",NVS_READONLY,&nvs)==ESP_OK) {


        /* The decoder has its own settings copy. Keep the input off the small
           startup stack as the append-only settings layout grows. */
        unsigned char *blob=malloc(sizeof(settings_t));if(!blob)boot_config_ok=false;
        for(uint16_t version=EMS_SETTINGS_VERSION;blob&&version>0;version--) {
            char key[16];snprintf(key,sizeof(key),"settings_v%u",(unsigned)version);

            size_t len=sizeof(settings_t);

            esp_err_t err=nvs_get_blob(nvs,key,blob,&len);

            if(err==ESP_ERR_NVS_NOT_FOUND) continue;

            if(err==ESP_OK && len>=sizeof(uint32_t))memcpy(&loaded_settings_version,blob,sizeof(uint32_t));
            if(err!=ESP_OK || !settings_decode(blob,len,&settings)){boot_config_ok=false;ESP_LOGW(TAG,"Stored settings rejected; defaults loaded");}

            break;

        }
        free(blob);

        float offset;size_t offset_len=sizeof(offset);
        if(nvs_get_blob(nvs,"auto_offset",&offset,&offset_len)==ESP_OK && offset_len==sizeof(offset) &&
           isfinite(offset) && offset>=0 && offset<=fminf(2,settings.min_charge_a-EMS_MIN_CHARGE_A))
            settings.current_offset_a=offset;

        nvs_close(nvs);

    }

    /* TeeNet supports automatic-direction RS485 modules only. Keep the
       legacy struct members for compatible NVS decoding, but never drive
       DE/RE pins from an old configuration. The power estimate uses cos phi 1. */
    settings.wallbox_rts_pin=-1;
    settings.xemex_rts_pin=-1;
    settings.house_rts_pin=-1;
    settings.power_factor=1;
    settings.wallbox_address=1;
    settings.house_xemex_coils=3;
    settings.reserved_http_meter_enabled=false;
    /* Repeated vehicle tests established 6.0 kW as the reliable three-phase
       floor. Normalize earlier 8.0/8.8 A commissioning values accordingly. */
    if(settings.min_charge_a>=8.0f && settings.min_charge_a<=8.81f)
        settings.min_charge_a=8.7f;
    if(settings.manual_current_a>0 && settings.manual_current_a<settings.min_charge_a)
        settings.manual_current_a=settings.min_charge_a;
    settings.reserved_http_meter_host[0]=0;
    settings.reserved_feedback_source=0;
    if(settings.mqtt_input_source==1)settings.mqtt_input_source=0;
    settings.homeassistant_enabled=settings.mqtt_enabled;
    saved_settings=settings;

}
static esp_err_t save_config(const settings_t *config) {

    if(!config_storage_ok) return ESP_FAIL;

    nvs_handle_t nvs; esp_err_t err=nvs_open("wallbox_ems",NVS_READWRITE,&nvs);

    if(err!=ESP_OK) return err;

    err=nvs_set_blob(nvs,EMS_SETTINGS_STORAGE_KEY,config,sizeof(*config));

    if(err==ESP_OK) err=nvs_commit(nvs);

    nvs_close(nvs); return err;

}
