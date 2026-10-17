/* GitHub access runs on demand in a low-priority task, never in the web loop. */
static struct {
    bool running,install,ok;unsigned progress;github_release_t release;
    char message[160];int64_t checked_at;
} github_update;
typedef struct {char location[2048];} github_headers_t;
static bool github_url_allowed(const char *url){
    const char *hosts[]={"https://api.github.com/","https://github.com/","https://release-assets.githubusercontent.com/","https://objects.githubusercontent.com/"};
    for(unsigned i=0;i<4;i++)if(!strncmp(url,hosts[i],strlen(hosts[i])))return true;
    return false;
}
static esp_err_t github_header_event(esp_http_client_event_t *e){
    if(e->event_id==HTTP_EVENT_ON_HEADER&&e->user_data&&!strcasecmp(e->header_key,"Location")){
        github_headers_t *h=e->user_data;
        if(strlen(e->header_value)>=sizeof(h->location))return ESP_FAIL;
        strcpy(h->location,e->header_value);
    }
    return ESP_OK;
}
static esp_http_client_handle_t github_open(const char *initial,int *status){
    char url[2048];if(strlen(initial)>=sizeof(url))return NULL;strcpy(url,initial);
    for(unsigned redirects=0;redirects<5;redirects++){
        if(!github_url_allowed(url))return NULL;
        github_headers_t h={0};
        esp_http_client_config_t cfg={.url=url,.timeout_ms=12000,.buffer_size=4096,.buffer_size_tx=1024,
            .crt_bundle_attach=esp_crt_bundle_attach,.disable_auto_redirect=true,.event_handler=github_header_event,.user_data=&h};
        esp_http_client_handle_t c=esp_http_client_init(&cfg);if(!c)return NULL;
        esp_http_client_set_header(c,"User-Agent","TeeNet/" EMS_VERSION);
        esp_http_client_set_header(c,"Accept","application/vnd.github+json");
        esp_http_client_set_header(c,"Accept-Encoding","identity");
        if(esp_http_client_open(c,0)!=ESP_OK||esp_http_client_fetch_headers(c)<0){esp_http_client_cleanup(c);return NULL;}
        *status=esp_http_client_get_status_code(c);
        if(*status==200){esp_http_client_set_user_data(c,NULL);return c;}
        bool redirect=(*status==301||*status==302||*status==303||*status==307||*status==308)&&h.location[0]&&github_url_allowed(h.location);
        if(redirect)strcpy(url,h.location);
        esp_http_client_cleanup(c);if(!redirect)return NULL;
    }
    return NULL;
}
static bool github_latest(github_release_t *release,char *error,size_t capacity){
    int status=0;esp_http_client_handle_t c=github_open("https://github.com/stetastic/TeeNet/releases/latest/download/release.json",&status);
    if(!c){snprintf(error,capacity,status==403||status==429?"GitHub-Limit erreicht. Spaeter erneut pruefen.":"GitHub nicht erreichbar. Internet, Uhrzeit und WLAN pruefen.");return false;}
    const size_t maximum=8192;char *body=malloc(maximum+1);size_t total=0;bool ok=body!=NULL;
    int64_t deadline=now_ms()+25000;
    while(ok&&total<maximum&&now_ms()<deadline){int n=esp_http_client_read(c,body+total,maximum-total);if(n<0){ok=false;break;}if(!n)break;total+=n;}
    ok=ok&&total<maximum&&esp_http_client_is_complete_data_received(c);esp_http_client_cleanup(c);
    const esp_partition_t *p=esp_ota_get_next_update_partition(NULL);
    if(ok){body[total]=0;ok=p&&github_release_parse(body,p->size,release);}free(body);
    if(!ok)snprintf(error,capacity,"GitHub-Datei fehlt oder passt nicht in diesen ESP. Keine Installation.");
    return ok;
}
static bool github_flash(const github_release_t *release,char *error,size_t capacity){
    int status=0;esp_http_client_handle_t c=github_open(release->url,&status);
    if(!c){snprintf(error,capacity,"Firmware-Download nicht erreichbar.");return false;}
    int64_t announced=esp_http_client_get_content_length(c);
    uint8_t prefix[EMS_FIRMWARE_PREFIX];size_t total=0;bool ok=announced<0||announced==release->bytes;
    while(ok&&total<sizeof(prefix)){int n=esp_http_client_read(c,(char *)prefix+total,sizeof(prefix)-total);if(n<=0)ok=false;else total+=n;}
    char version[32];ok=ok&&firmware_prefix_check(prefix,sizeof(prefix),version)&&!strcmp(version,release->version);
    esp_ota_handle_t handle=0;bool opened=false;uint8_t *buffer=NULL;const esp_partition_t *p=esp_ota_get_next_update_partition(NULL);
    if(ok){opened=p&&release->bytes<=p->size&&esp_ota_begin(p,release->bytes,&handle)==ESP_OK;buffer=opened?malloc(4096):NULL;ok=buffer&&esp_ota_write(handle,prefix,sizeof(prefix))==ESP_OK;}
    mbedtls_sha256_context hash;mbedtls_sha256_init(&hash);
    if(ok)ok=mbedtls_sha256_starts(&hash,0)==0&&mbedtls_sha256_update(&hash,prefix,sizeof(prefix))==0;
    int64_t deadline=now_ms()+180000;
    while(ok&&total<release->bytes&&now_ms()<deadline){
        size_t want=release->bytes-total;if(want>4096)want=4096;
        int n=esp_http_client_read(c,(char *)buffer,want);
        if(n<=0||esp_ota_write(handle,buffer,n)!=ESP_OK||mbedtls_sha256_update(&hash,buffer,n)!=0){ok=false;break;}
        total+=n;LOCK();github_update.progress=(unsigned)(total*100/release->bytes);UNLOCK();
        vTaskDelay(1);
    }
    /* Reject truncation and unexpected trailing bytes before selecting boot. */
    if(ok&&total==release->bytes){char extra;int n=esp_http_client_read(c,&extra,1);ok=n==0&&esp_http_client_is_complete_data_received(c);}else ok=false;
    uint8_t digest[32];char hex[65];
    if(ok){ok=mbedtls_sha256_finish(&hash,digest)==0;for(int i=0;i<32;i++)snprintf(hex+i*2,3,"%02x",digest[i]);ok=ok&&!strcmp(hex,release->sha256);}
    mbedtls_sha256_free(&hash);free(buffer);esp_http_client_cleanup(c);
    if(!ok){if(opened)esp_ota_abort(handle);snprintf(error,capacity,"Download oder Pruefsumme fehlerhaft. Bisherige Firmware bleibt aktiv.");return false;}
    ok=esp_ota_end(handle)==ESP_OK;if(ok)ok=esp_ota_set_boot_partition(p)==ESP_OK;
    if(!ok)snprintf(error,capacity,"Firmware-Pruefung fehlgeschlagen. Bisherige Firmware bleibt aktiv.");
    return ok;
}
static void github_worker(void *arg){
    bool install=(uintptr_t)arg!=0;github_release_t previous,release={0};char message[160]={0};
    LOCK();previous=github_update.release;UNLOCK();
    bool ok=github_latest(&release,message,sizeof(message));
    if(ok&&install){
        if(strcmp(previous.sha256,release.sha256)||strcmp(previous.version,release.version)||previous.bytes!=release.bytes){ok=false;strcpy(message,"GitHub-Version wurde geaendert. Erneut pruefen.");}
        else ok=github_flash(&release,message,sizeof(message));
    }
    LOCK();
    if(ok){github_update.release=release;github_update.checked_at=now_ms();snprintf(message,sizeof(message),install?"Datei geprueft. Neustart laeuft; Ladung bleibt aus.":"GitHub-Version %s verfuegbar.",release.version);}
    github_update.ok=ok;github_update.running=false;strcpy(github_update.message,message);
    if(install){ota_in_progress=false;if(ok)restarting=true;}
    UNLOCK();vTaskDelete(NULL);
}
static esp_err_t github_get(httpd_req_t *req){
    cJSON *o=cJSON_CreateObject();if(!o)return send_json(req,NULL);LOCK();
    cJSON_AddBoolToObject(o,"running",github_update.running);cJSON_AddBoolToObject(o,"installing",github_update.install);
    cJSON_AddBoolToObject(o,"ok",github_update.ok);cJSON_AddNumberToObject(o,"progress",github_update.progress);
    cJSON_AddStringToObject(o,"message",github_update.message);cJSON_AddStringToObject(o,"version",github_update.release.version);
    cJSON_AddStringToObject(o,"sha256",github_update.release.sha256);cJSON_AddNumberToObject(o,"bytes",github_update.release.bytes);
    cJSON_AddNumberToObject(o,"checked_age_s",github_update.checked_at?(now_ms()-github_update.checked_at)/1000:-1);
    cJSON_AddBoolToObject(o,"newer",github_update.ok&&firmware_version_compare(github_update.release.version,EMS_VERSION)>0);
    UNLOCK();return send_json(req,o);
}
static esp_err_t github_post(httpd_req_t *req){
    if(!authorized(req))return ESP_OK;
    cJSON *o=read_json(req);
    if(!o)return ESP_OK;
    cJSON *a=cJSON_GetObjectItemCaseSensitive(o,"action");bool install=cJSON_IsString(a)&&!strcmp(a->valuestring,"install");
    bool valid=cJSON_IsString(a)&&(install||!strcmp(a->valuestring,"check"))&&cJSON_GetArraySize(o)==1;cJSON_Delete(o);
    if(!valid)return httpd_resp_send_err(req,HTTPD_400_BAD_REQUEST,"Update-Aktion ungueltig.");
    LOCK();int64_t now=now_ms();bool allowed=wifi_online&&!restarting&&!ota_in_progress&&!github_update.running;
    if(install)allowed=allowed&&github_update.ok&&now-github_update.checked_at<600000&&
        firmware_version_compare(github_update.release.version,EMS_VERSION)>0&&!settings.enabled&&
        !(fresh(now,actual_at,EMS_METER_TTL)&&(actual_a[0]>=1||actual_a[1]>=1||actual_a[2]>=1));
    if(allowed){github_update.running=true;github_update.install=install;github_update.progress=0;
        strcpy(github_update.message,install?"Firmware wird von GitHub geladen.":"GitHub wird geprueft.");if(install){ota_in_progress=true;settings.enabled=false;}}
    UNLOCK();
    if(!allowed){httpd_resp_set_status(req,"409 Conflict");return httpd_resp_sendstr(req,install?"Version erneut pruefen und Ladung zuerst stoppen.":"Heimnetz verbinden und laufende Wartung abwarten.");}
    if(xTaskCreate(github_worker,"github_update",10240,(void *)(uintptr_t)install,1,NULL)!=pdPASS){LOCK();github_update.running=false;if(install)ota_in_progress=false;UNLOCK();return httpd_resp_send_err(req,HTTPD_500_INTERNAL_SERVER_ERROR,"Zu wenig Arbeitsspeicher fuer GitHub-Update.");}
    return github_get(req);
}
