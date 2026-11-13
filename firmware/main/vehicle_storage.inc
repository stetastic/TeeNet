/* Profiles and their photos share one atomic blob. Old separate keys remain readable. */
typedef struct {
    uint32_t version;
    vehicle_store_t store;
    uint16_t photo_size[VEHICLE_COUNT];
    unsigned char photos[VEHICLE_COUNT][VEHICLE_PHOTO_BYTES];
} vehicle_assets_t;
static vehicle_assets_t *vehicle_restore_pending;
static bool vehicle_assets_valid(const vehicle_assets_t *a){
    if(!a||a->version!=1||!vehicle_store_valid(&a->store))return false;
    for(unsigned i=0;i<VEHICLE_COUNT;i++){
        size_t n=a->photo_size[i];const unsigned char *p=a->photos[i];
        if(n&&(n<4||n>VEHICLE_PHOTO_BYTES||p[0]!=0xff||p[1]!=0xd8||p[n-2]!=0xff||p[n-1]!=0xd9))return false;
    }
    return true;
}
static vehicle_assets_t *vehicle_assets_read(void){
    vehicle_assets_t *a=calloc(1,sizeof(*a));if(!a)return NULL;
    a->version=1;LOCK();a->store=vehicles;UNLOCK();
    nvs_handle_t h;esp_err_t e=nvs_open_from_partition("stats","vehicles",NVS_READONLY,&h);
    if(e==ESP_ERR_NVS_NOT_FOUND)return a;
    if(e!=ESP_OK){free(a);return NULL;}
    size_t n=0;e=nvs_get_blob(h,"bundle_v1",NULL,&n);
    if(e==ESP_OK){
        size_t header=offsetof(vehicle_assets_t,photos);unsigned char *packed=n>=header&&n<=sizeof(*a)?malloc(n):NULL;
        if(!packed){nvs_close(h);free(a);return NULL;}
        e=nvs_get_blob(h,"bundle_v1",packed,&n);bool valid=e==ESP_OK;size_t offset=header;
        if(valid){memcpy(a,packed,header);for(unsigned i=0;i<VEHICLE_COUNT;i++){size_t count=a->photo_size[i];if(count>VEHICLE_PHOTO_BYTES||count>n-offset){valid=false;break;}memcpy(a->photos[i],packed+offset,count);offset+=count;}}
        free(packed);if(!valid||offset!=n){nvs_close(h);free(a);return NULL;}n=sizeof(*a);
    }
    if(e==ESP_ERR_NVS_NOT_FOUND){
        for(unsigned i=0;i<VEHICLE_COUNT;i++){
            char key[8];snprintf(key,sizeof(key),"photo%u",i);size_t bytes=VEHICLE_PHOTO_BYTES;
            e=nvs_get_blob(h,key,a->photos[i],&bytes);
            if(e==ESP_OK)a->photo_size[i]=(uint16_t)bytes;
            else if(e!=ESP_ERR_NVS_NOT_FOUND){nvs_close(h);free(a);return NULL;}
        }
        e=ESP_OK;n=sizeof(*a);
    }
    nvs_close(h);
    if(e!=ESP_OK||n!=sizeof(*a)||!vehicle_assets_valid(a)){free(a);return NULL;}
    return a;
}
static bool vehicle_assets_commit(const vehicle_assets_t *a){
    if(!stats_storage_ok||!vehicle_assets_valid(a))return false;
    nvs_handle_t h;esp_err_t e=nvs_open_from_partition("stats","vehicles",NVS_READWRITE,&h);
    if(e!=ESP_OK)return false;
    size_t header=offsetof(vehicle_assets_t,photos),bytes=header;for(unsigned i=0;i<VEHICLE_COUNT;i++)bytes+=a->photo_size[i];
    unsigned char *packed=malloc(bytes);if(!packed){nvs_close(h);return false;}memcpy(packed,a,header);size_t offset=header;
    for(unsigned i=0;i<VEHICLE_COUNT;i++){memcpy(packed+offset,a->photos[i],a->photo_size[i]);offset+=a->photo_size[i];}
    e=nvs_set_blob(h,"bundle_v1",packed,bytes);free(packed);if(e==ESP_OK)e=nvs_commit(h);
    if(e==ESP_OK){for(unsigned i=0;i<VEHICLE_COUNT;i++){char key[8];snprintf(key,sizeof(key),"photo%u",i);nvs_erase_key(h,key);}nvs_commit(h);}
    nvs_close(h);return e==ESP_OK;
}
static void vehicle_assets_boot(void){
    vehicle_assets_t *a=vehicle_assets_read();
    if(a){vehicles=a->store;free(a);}else storage_error=true;
}
static bool vehicle_restore_commit(void){
    if(!vehicle_restore_pending)return true;
    if(xSemaphoreTake(maintenance_flash_lock,ticks(2000))!=pdTRUE)return false;
    bool okay=vehicle_assets_commit(vehicle_restore_pending);
    if(okay){LOCK();vehicles=vehicle_restore_pending->store;vehicle_revision++;memset(vehicle_soc_at,0,sizeof(vehicle_soc_at));car_soc_at=0;UNLOCK();}
    xSemaphoreGive(maintenance_flash_lock);return okay;
}
static cJSON *vehicle_assets_json(void){
    if(xSemaphoreTake(maintenance_flash_lock,ticks(2000))!=pdTRUE)return NULL;
    vehicle_assets_t *a=vehicle_assets_read();xSemaphoreGive(maintenance_flash_lock);if(!a)return NULL;
    cJSON *root=cJSON_CreateObject(),*rows=cJSON_CreateArray(),*photos=cJSON_CreateArray();bool ok=root&&rows&&photos;
    if(ok)ok=cJSON_AddNumberToObject(root,"version",1)&&cJSON_AddBoolToObject(root,"enabled",a->store.enabled);
    for(unsigned i=0;ok&&i<VEHICLE_COUNT;i++){
        const vehicle_profile_t *p=&a->store.rows[i];cJSON *r=cJSON_CreateObject(),*cards=cJSON_CreateArray();
        ok=r&&cards;
        if(ok)ok=cJSON_AddStringToObject(r,"name",p->name)&&cJSON_AddStringToObject(r,"plate",p->plate)&&cJSON_AddNumberToObject(r,"capacity_kwh",p->capacity_kwh)&&cJSON_AddNumberToObject(r,"maximum_kw",p->maximum_kw)&&cJSON_AddNumberToObject(r,"efficiency",p->efficiency)&&cJSON_AddNumberToObject(r,"target_soc",p->target_soc)&&cJSON_AddNumberToObject(r,"soc_source",p->soc_source)&&cJSON_AddNumberToObject(r,"picture",p->picture);
        for(unsigned j=0;ok&&j<2;j++)ok=json_add_owned_to_array(cards,cJSON_CreateString(p->cards[j]));
        if(ok){ok=json_add_owned_to_object(r,"cards",cards);cards=NULL;}
        if(ok){ok=json_add_owned_to_array(rows,r);r=NULL;}
        cJSON_Delete(cards);cJSON_Delete(r);
        unsigned char *encoded=malloc(2049);size_t length=0;
        if(!encoded){ok=false;break;}
        if(a->photo_size[i]){if(mbedtls_base64_encode(encoded,2049,&length,a->photos[i],a->photo_size[i])!=0)ok=false;}
        encoded[length]=0;if(ok)ok=json_add_owned_to_array(photos,cJSON_CreateString((const char *)encoded));free(encoded);
    }
    if(ok){ok=json_add_owned_to_object(root,"profiles",rows);rows=NULL;}
    if(ok){ok=json_add_owned_to_object(root,"photos",photos);photos=NULL;}
    free(a);cJSON_Delete(rows);cJSON_Delete(photos);if(!ok){cJSON_Delete(root);root=NULL;}return root;
}
static vehicle_assets_t *vehicle_assets_parse(const cJSON *root){
    const cJSON *v=cJSON_GetObjectItemCaseSensitive(root,"version"),*enabled=cJSON_GetObjectItemCaseSensitive(root,"enabled"),*rows=cJSON_GetObjectItemCaseSensitive(root,"profiles"),*photos=cJSON_GetObjectItemCaseSensitive(root,"photos");
    if(!cJSON_IsObject(root)||cJSON_GetArraySize(root)!=4||!cJSON_IsNumber(v)||v->valuedouble!=1||!cJSON_IsBool(enabled)||!cJSON_IsArray(rows)||cJSON_GetArraySize(rows)!=VEHICLE_COUNT||!cJSON_IsArray(photos)||cJSON_GetArraySize(photos)!=VEHICLE_COUNT)return NULL;
    vehicle_assets_t *a=calloc(1,sizeof(*a));if(!a)return NULL;a->version=1;a->store.version=1;a->store.enabled=cJSON_IsTrue(enabled);bool okay=true;
    for(unsigned i=0;okay&&i<VEHICLE_COUNT;i++){
        okay=vehicle_profile_parse(cJSON_GetArrayItem(rows,i),&a->store.rows[i]);
        const cJSON *photo=cJSON_GetArrayItem(photos,i);size_t n=0;
        okay=okay&&cJSON_IsString(photo)&&strlen(photo->valuestring)<=2048;
        if(okay&&photo->valuestring[0])okay=mbedtls_base64_decode(a->photos[i],VEHICLE_PHOTO_BYTES,&n,(const unsigned char *)photo->valuestring,strlen(photo->valuestring))==0;
        a->photo_size[i]=(uint16_t)n;
    }
    if(!okay||!vehicle_assets_valid(a)){free(a);return NULL;}return a;
}
