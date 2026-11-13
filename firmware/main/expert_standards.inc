/* Device-local comparison defaults. Stable IDs survive appended parameters;
   these values never activate control parameters or GPIOs. */
typedef struct {uint32_t version;char id[48];float value;} expert_standard_record_t;
static void expert_standard_key(const char *id,bool gpio,char key[12]){
 uint32_t hash=2166136261u;for(const unsigned char *s=(const unsigned char *)id;*s;s++)hash=(hash^*s)*16777619u;
 snprintf(key,12,"%c%08lx",gpio?'g':'p',(unsigned long)hash);
}
static bool expert_standard_valid(unsigned index,bool gpio,float v){
 if(!isfinite(v))return false;
 if(!gpio)return v>=expert_meta[index].minimum&&v<=expert_meta[index].maximum;
 return v>=-1&&v<=48&&floorf(v)==v&&((v==-1&&index==EMS_GPIO_ROLES-1)||(v>=0&&!settings_gpio_reason((int)v,index==EMS_GPIO_ROLES-1)[0]));
}
static void expert_standards_load(void){
 nvs_handle_t nvs;if(!config_storage_ok||nvs_open("expert_std",NVS_READONLY,&nvs)!=ESP_OK)return;
 for(unsigned group=0;group<2;group++)for(unsigned i=0;i<(group?EMS_GPIO_ROLES:EP_COUNT);i++){
  const char *id=group?gpio_roles[i].id:expert_meta[i].id;char key[12];expert_standard_key(id,group,key);
  expert_standard_record_t record;size_t size=sizeof(record);
  if(nvs_get_blob(nvs,key,&record,&size)!=ESP_OK||size!=sizeof(record)||record.version!=1||!memchr(record.id,0,sizeof(record.id))||strcmp(record.id,id)||!expert_standard_valid(i,group,record.value))continue;
  if(group){gpio_standard_set[i]=true;gpio_standard_saved[i]=(int)record.value;}else{expert_standard_set[i]=true;expert_standard_saved[i]=record.value;}
 }
 nvs_close(nvs);
}
static esp_err_t expert_standard_post(httpd_req_t *req,cJSON *o){
 const cJSON *id=cJSON_GetObjectItemCaseSensitive(o,"id"),*value=cJSON_GetObjectItemCaseSensitive(o,"value");
 bool valid=cJSON_GetArraySize(o)==4&&cJSON_IsString(id)&&cJSON_IsNumber(value);unsigned index=0;bool gpio=false;
 if(valid){for(index=0;index<EP_COUNT&&strcmp(expert_meta[index].id,id->valuestring);index++){}if(index==EP_COUNT){gpio=true;for(index=0;index<EMS_GPIO_ROLES&&strcmp(gpio_roles[index].id,id->valuestring);index++){}valid=index<EMS_GPIO_ROLES;}}
 valid=valid&&expert_standard_valid(index,gpio,valid?(float)value->valuedouble:NAN);
 expert_standard_record_t record={.version=1};if(valid){snprintf(record.id,sizeof(record.id),"%s",id->valuestring);record.value=(float)value->valuedouble;}cJSON_Delete(o);
 if(!valid)return httpd_resp_send_err(req,HTTPD_400_BAD_REQUEST,"Gueltigen Expertenwert oder GPIO fuer den Standard waehlen.");
 if(xSemaphoreTake(maintenance_flash_lock,ticks(2000))!=pdTRUE){httpd_resp_set_status(req,"409 Conflict");return httpd_resp_sendstr(req,"Speicherung beschaeftigt.");}
 LOCK();bool stopped=expert_stopped_locked();UNLOCK();
 esp_err_t error=ESP_ERR_INVALID_STATE;
 if(stopped){nvs_handle_t nvs;error=config_storage_ok?nvs_open("expert_std",NVS_READWRITE,&nvs):ESP_ERR_INVALID_STATE;
  if(error==ESP_OK){char key[12];expert_standard_key(record.id,gpio,key);error=nvs_set_blob(nvs,key,&record,sizeof(record));if(error==ESP_OK)error=nvs_commit(nvs);nvs_close(nvs);}}
 if(error==ESP_OK){LOCK();if(gpio){gpio_standard_set[index]=true;gpio_standard_saved[index]=(int)record.value;}else{expert_standard_set[index]=true;expert_standard_saved[index]=record.value;}UNLOCK();}
 xSemaphoreGive(maintenance_flash_lock);
 if(error!=ESP_OK)return httpd_resp_send_err(req,HTTPD_500_INTERNAL_SERVER_ERROR,"Persoenliche Vorgabe nicht gespeichert. Ladestopp und Speicher pruefen.");
 return ok_json(req);
}
