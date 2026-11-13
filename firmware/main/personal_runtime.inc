/* Owner-only opt-ins, separate from the public settings layout/NVS migration. */
static bool personal_voltage_enabled,personal_solis_enabled;
static float personal_voltage[3];
static int64_t personal_voltage_at;
static void personal_sync_locked(int64_t now){
    if(selected_meter_voltage_locked(now))return;
    bool valid=personal_voltage_enabled&&settings.mqtt_enabled&&mqtt_online&&
        !strcmp(settings.wallbox_meter_type,"xemex")&&personal_voltage_at>0&&
        now>=personal_voltage_at&&now-personal_voltage_at<30000;
    ems_phase_voltage_override(personal_voltage,valid);
}
static void personal_load(void){
    nvs_handle_t nvs;uint32_t flags=0;
    if(config_storage_ok&&nvs_open("personal",NVS_READONLY,&nvs)==ESP_OK){
        if(nvs_get_u32(nvs,"options",&flags)!=ESP_OK||flags>3)flags=0;
        nvs_close(nvs);
    }
    personal_voltage_enabled=(flags&1)!=0;personal_solis_enabled=(flags&2)!=0;
    ems_phase_voltage_override(NULL,false);
}
static bool personal_voltage_message_locked(const char *body){
    if(!personal_voltage_enabled)return false;
    cJSON *o=cJSON_ParseWithOpts(body,NULL,true);if(!cJSON_IsObject(o)){cJSON_Delete(o);return false;}
    cJSON *valid=cJSON_GetObjectItemCaseSensitive(o,"valid"),*v=cJSON_GetObjectItemCaseSensitive(o,"v"),*stamp=cJSON_GetObjectItemCaseSensitive(o,"ts");
    bool ok=cJSON_IsTrue(valid)&&cJSON_IsArray(v)&&cJSON_GetArraySize(v)==3&&cJSON_IsNumber(stamp);
    double age=cJSON_IsNumber(stamp)?(double)time(NULL)-stamp->valuedouble:1e9;
    ok=ok&&isfinite(stamp->valuedouble)&&floor(stamp->valuedouble)==stamp->valuedouble&&age>=-5&&age<30;
    float next[3]={0};for(unsigned i=0;i<3&&ok;i++){cJSON *x=cJSON_GetArrayItem(v,i);ok=cJSON_IsNumber(x)&&isfinite(x->valuedouble)&&x->valuedouble>=180&&x->valuedouble<=270;if(ok)next[i]=x->valuedouble;}
    if(ok){memcpy(personal_voltage,next,sizeof(next));personal_voltage_at=now_ms()-(int64_t)(fmax(0,age)*1000);}
    else personal_voltage_at=0;
    bool accepted=ok||cJSON_IsFalse(valid);personal_sync_locked(now_ms());cJSON_Delete(o);return accepted;
}
