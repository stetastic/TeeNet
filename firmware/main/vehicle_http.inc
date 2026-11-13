static cJSON *vehicle_snapshot(void){
    cJSON *o=cJSON_CreateObject();if(!o)return NULL;
    LOCK();int64_t now=now_ms(),epoch,midnight;calendar(&epoch,&midnight);
    cJSON_AddBoolToObject(o,"enabled",vehicles.enabled);
    cJSON_AddBoolToObject(o,"manual_soc_prompt",settings.vehicle_manual_soc_prompt);
    cJSON_AddNumberToObject(o,"fixed_profile",settings.vehicle_fixed_profile);
    cJSON_AddNumberToObject(o,"revision",vehicle_revision);
    cJSON_AddNumberToObject(o,"recognized_slot",vehicle_current_locked(now)+1);
    cJSON_AddStringToObject(o,"detected_card",vehicle_http_fresh_locked(now)?evcc_shell.session.card:"");
    cJSON_AddBoolToObject(o,"wallbox_fresh",vehicle_http_fresh_locked(now));
    nullable(o,"wallbox_session_kwh",evcc_shell.session.consumed_wh/1000,vehicle_http_fresh_locked(now)&&evcc_shell.session.energy_valid);
    cJSON *rows=cJSON_AddArrayToObject(o,"profiles");
    for(unsigned i=0;i<VEHICLE_COUNT;i++){
        const vehicle_profile_t *p=&vehicles.rows[i];cJSON *v=cJSON_CreateObject();
        cJSON_AddStringToObject(v,"name",p->name);cJSON_AddStringToObject(v,"plate",p->plate);
        cJSON_AddNumberToObject(v,"capacity_kwh",p->capacity_kwh);cJSON_AddNumberToObject(v,"maximum_kw",p->maximum_kw);
        cJSON_AddNumberToObject(v,"efficiency",p->efficiency);cJSON_AddNumberToObject(v,"target_soc",p->target_soc);cJSON_AddNumberToObject(v,"soc_source",p->soc_source);
        cJSON_AddNumberToObject(v,"picture",p->picture);
        cJSON *cards=cJSON_AddArrayToObject(v,"cards");for(unsigned j=0;j<2;j++)json_add_owned_to_array(cards,cJSON_CreateString(p->cards[j]));
        bool measured=vehicle_soc_fresh_locked(i,now);float estimated=0;
        bool guessed=!measured&&vehicle_estimate_soc_locked(i,now,&estimated);
        nullable(v,"soc",measured?vehicle_soc_value_locked(i):estimated,measured||guessed);
        cJSON_AddBoolToObject(v,"soc_estimated",guessed);
        char topic[128];snprintf(topic,sizeof(topic),"%s/input/vehicle_%u_soc_pct",settings.mqtt_prefix,i+1);cJSON_AddStringToObject(v,"soc_topic",topic);
        json_add_owned_to_array(rows,v);
    }
    cJSON *plan=cJSON_AddObjectToObject(o,"plan");
    cJSON_AddBoolToObject(plan,"active",vehicle_plan.active&&charge_plan.active);cJSON_AddNumberToObject(plan,"slot",vehicle_plan.version?vehicle_plan.slot+1:0);
    cJSON_AddStringToObject(plan,"reason",vehicle_plan_reason);cJSON_AddNumberToObject(plan,"target_soc",vehicle_plan.target_soc);
    cJSON_AddBoolToObject(plan,"measured_soc",vehicle_plan.active&&vehicle_soc_fresh_locked(vehicle_plan.slot,now));
    float remaining=fmaxf(0,(charge_plan.target_wh-charge_plan.delivered_wh)/1000);
    float maximum=vehicle_plan_maximum_locked(charge_plan_maximum_kw(&settings,house_currents_ready_locked(now),evu_contact.stable));
    cJSON_AddNumberToObject(plan,"maximum_kw",maximum);
    cJSON_AddNumberToObject(plan,"grid_start_epoch",vehicle_plan.active?vehicle_grid_start(charge_plan.deadline_s,remaining,maximum):0);
    double seconds=vehicle_seconds(remaining,maximum);
    nullable(plan,"earliest_finish_epoch",epoch/1000+ceil(seconds),vehicle_plan.active&&isfinite(seconds));
    cJSON_AddBoolToObject(plan,"deadline_feasible",vehicle_plan.active&&isfinite(seconds)&&epoch/1000+seconds<=charge_plan.deadline_s);
    float estimate=vehicle_plan.last_soc+(charge_plan.delivered_wh-vehicle_plan.anchor_wh)/1000*vehicle_plan.efficiency/fmaxf(.001f,vehicle_plan.capacity_kwh)*100;
    if(vehicle_plan.active&&vehicle_soc_fresh_locked(vehicle_plan.slot,now))estimate=vehicle_soc_value_locked(vehicle_plan.slot);
    nullable(plan,"estimated_soc",fminf(100,estimate),vehicle_plan.version!=0);
    UNLOCK();return o;
}
static esp_err_t vehicles_get(httpd_req_t *req){if(!authorized(req))return ESP_OK;return send_json(req,vehicle_snapshot());}
static esp_err_t vehicles_post(httpd_req_t *req){
    if(!authorized(req))return ESP_OK;
    cJSON *o=read_json(req);if(!o)return ESP_OK;
    cJSON *slot=cJSON_GetObjectItem(o,"slot"),*enabled=cJSON_GetObjectItem(o,"enabled"),*revision=cJSON_GetObjectItem(o,"revision");
    unsigned index=cJSON_IsNumber(slot)?(unsigned)slot->valueint:VEHICLE_COUNT;
    bool feature_only=!slot&&!cJSON_GetObjectItem(o,"profile")&&cJSON_GetArraySize(o)==2;
    bool valid=cJSON_IsBool(enabled)&&cJSON_IsNumber(revision)&&(feature_only||
        (cJSON_IsNumber(slot)&&slot->valuedouble==slot->valueint&&index>=1&&index<=VEHICLE_COUNT&&cJSON_GetArraySize(o)==4));
    vehicle_store_t *next=malloc(sizeof(*next));if(!next){cJSON_Delete(o);return httpd_resp_send_err(req,HTTPD_500_INTERNAL_SERVER_ERROR,"Speicher belegt. Erneut versuchen.");}
    LOCK();*next=vehicles;bool unchanged=cJSON_IsNumber(revision)&&revision->valuedouble==vehicle_revision;UNLOCK();
    if(valid){next->enabled=cJSON_IsTrue(enabled);valid=(feature_only||vehicle_profile_parse(cJSON_GetObjectItem(o,"profile"),&next->rows[index-1]))&&vehicle_store_valid(next);}
    cJSON_Delete(o);
    LOCK();bool deleting_fixed=valid&&!feature_only&&settings.vehicle_fixed_profile==index&&!next->rows[index-1].name[0];UNLOCK();
    if(deleting_fixed){free(next);httpd_resp_set_status(req,"409 Conflict");return httpd_resp_sendstr(req,"Vor dem Loeschen ein anderes festes Profil oder Ladekartenerkennung waehlen und speichern.");}
    if(!valid||!unchanged){free(next);httpd_resp_set_status(req,unchanged?"400 Bad Request":"409 Conflict");return httpd_resp_sendstr(req,unchanged?"Profil unvollstaendig oder Ladekarte bereits zugeordnet.":"Profile wurden inzwischen geaendert. Neu laden.");}
    if(xSemaphoreTake(maintenance_flash_lock,ticks(1500))!=pdTRUE){free(next);return httpd_resp_send_err(req,HTTPD_500_INTERNAL_SERVER_ERROR,"Speicherung beschaeftigt.");}
    LOCK();bool allowed=config_storage_ok&&!charge_plan.active&&!restarting&&!ota_in_progress&&!backup_in_progress&&!boot_validation_pending;UNLOCK();
    vehicle_assets_t *assets=allowed?vehicle_assets_read():NULL;esp_err_t error=ESP_ERR_INVALID_STATE;
    if(assets){assets->store=*next;error=vehicle_assets_commit(assets)?ESP_OK:ESP_ERR_INVALID_STATE;free(assets);}
    if(error==ESP_OK){LOCK();vehicles=*next;vehicle_revision++;memset(vehicle_soc_at,0,sizeof(vehicle_soc_at));UNLOCK();}
    xSemaphoreGive(maintenance_flash_lock);free(next);
    if(error!=ESP_OK)return httpd_resp_send_err(req,HTTPD_500_INTERNAL_SERVER_ERROR,allowed?"Profil nicht gespeichert. Speicher pruefen.":"Ladeplan oder Wartung zuerst beenden.");
    return send_json(req,vehicle_snapshot());
}
static int vehicle_photo_slot(httpd_req_t *req){
    char query[64],id[8];if(httpd_req_get_url_query_str(req,query,sizeof(query))!=ESP_OK||httpd_query_key_value(query,"slot",id,sizeof(id))!=ESP_OK)return -1;
    if(strlen(id)!=1||id[0]<'1'||id[0]>'0'+VEHICLE_COUNT)return -1;
    return id[0]-'1';
}
static esp_err_t vehicle_photo_get(httpd_req_t *req){
    int slot=vehicle_photo_slot(req);if(slot<0)return httpd_resp_send_err(req,HTTPD_400_BAD_REQUEST,"Fahrzeug fehlt.");
    vehicle_assets_t *assets=vehicle_assets_read();if(!assets)return httpd_resp_send_err(req,HTTPD_500_INTERNAL_SERVER_ERROR,"Fahrzeugdaten nicht lesbar.");
    size_t n=assets->photo_size[slot];if(!n){free(assets);return httpd_resp_send_err(req,HTTPD_404_NOT_FOUND,"Kein Fahrzeugbild.");}
    httpd_resp_set_type(req,"image/jpeg");httpd_resp_set_hdr(req,"Cache-Control","no-store");httpd_resp_set_hdr(req,"X-Content-Type-Options","nosniff");
    esp_err_t e=httpd_resp_send(req,(const char *)assets->photos[slot],n);free(assets);return e;
}
static esp_err_t vehicle_photo_post(httpd_req_t *req){
    if(!authorized(req))return ESP_OK;
    int slot=vehicle_photo_slot(req);cJSON *o=read_json(req);if(!o)return ESP_OK;
    cJSON *photo=cJSON_GetObjectItem(o,"jpeg_base64");unsigned char *data=malloc(VEHICLE_PHOTO_BYTES);size_t n=0;
    bool valid=slot>=0&&cJSON_GetArraySize(o)==1&&cJSON_IsString(photo)&&data&&strlen(photo->valuestring)<=2048;
    bool remove=valid&&!photo->valuestring[0];
    if(valid&&!remove)valid=mbedtls_base64_decode(data,VEHICLE_PHOTO_BYTES,&n,(const unsigned char *)photo->valuestring,strlen(photo->valuestring))==0&&n>=4&&data[0]==0xff&&data[1]==0xd8&&data[n-2]==0xff&&data[n-1]==0xd9;
    cJSON_Delete(o);if(!valid){free(data);return httpd_resp_send_err(req,HTTPD_400_BAD_REQUEST,"JPEG-Vorschaubild bis 1536 Bytes auswaehlen.");}
    if(xSemaphoreTake(maintenance_flash_lock,ticks(1500))!=pdTRUE){free(data);return httpd_resp_send_err(req,HTTPD_500_INTERNAL_SERVER_ERROR,"Speicherung beschaeftigt.");}
    LOCK();bool allowed=stats_storage_ok&&!restarting&&!ota_in_progress&&!backup_in_progress&&!boot_validation_pending;UNLOCK();
    vehicle_assets_t *assets=allowed?vehicle_assets_read():NULL;esp_err_t error=ESP_ERR_INVALID_STATE;
    if(assets){assets->photo_size[slot]=(uint16_t)(remove?0:n);if(!remove)memcpy(assets->photos[slot],data,n);error=vehicle_assets_commit(assets)?ESP_OK:ESP_ERR_INVALID_STATE;free(assets);}
    xSemaphoreGive(maintenance_flash_lock);free(data);
    if(error!=ESP_OK)return httpd_resp_send_err(req,HTTPD_500_INTERNAL_SERVER_ERROR,"Bild nicht gespeichert. Speicher oder Wartung pruefen.");
    return ok_json(req);
}
static esp_err_t vehicle_plan_post(httpd_req_t *req){
    if(!authorized(req))return ESP_OK;
    cJSON *o=read_json(req);if(!o)return ESP_OK;
    const cJSON *slot=cJSON_GetObjectItem(o,"slot"),*start=cJSON_GetObjectItem(o,"current_soc"),*target=cJSON_GetObjectItem(o,"target_soc"),*deadline=cJSON_GetObjectItem(o,"deadline_epoch"),*resume=cJSON_GetObjectItem(o,"resume");
    const cJSON *mode=cJSON_GetObjectItem(o,"mode");bool manual=mode&&cJSON_IsString(mode)&&!strcmp(mode->valuestring,"manual");
    bool resuming=cJSON_IsTrue(resume);int index=cJSON_IsNumber(slot)&&slot->valueint>=1&&slot->valueint<=VEHICLE_COUNT?slot->valueint-1:-1;
    bool valid=index>=0&&index<VEHICLE_COUNT&&slot->valuedouble==slot->valueint&&cJSON_IsNumber(start)&&cJSON_IsNumber(target)&&cJSON_IsNumber(deadline)&&isfinite(deadline->valuedouble)&&floor(deadline->valuedouble)==deadline->valuedouble&&(!resume||cJSON_IsBool(resume))&&cJSON_GetArraySize(o)==4+(resume?1:0)+(mode?1:0)&&(!mode||(cJSON_IsString(mode)&&(!strcmp(mode->valuestring,"manual")||!strcmp(mode->valuestring,"pv"))));
    int64_t epoch,midnight;bool clock_ok=calendar(&epoch,&midnight)!=0;
    LOCK();int64_t now=now_ms();if(resuming&&!mode)manual=soc_goal.manual;
    const char *problem=valid?plan_start_problem_locked(now,clock_ok,manual):"Fahrzeug-Ladeplandaten ungueltig.";
    if(!problem&&resuming&&mode&&manual!=soc_goal.manual)problem="Betriebsart zum Fortsetzen beibehalten oder einen neuen Ladeplan starten.";
    valid=valid&&!problem&&vehicles.enabled&&!evcc_control.configured&&!evcc_configuring&&vehicle_http_fresh_locked(now)&&(settings.vehicle_fixed_profile||evcc_shell.session.present)&&(evcc_shell.state=='B'||evcc_shell.state=='C');
    float kwh=0,soc=0;
    if(valid){
        bool measured=vehicle_soc_fresh_locked(index,now);soc=measured?vehicle_soc_value_locked(index):(float)start->valuedouble;
        int current=vehicle_current_locked(now);
        valid=current==index&&
            vehicle_need(&vehicles.rows[index],soc,(float)target->valuedouble,&kwh)&&deadline->valuedouble>epoch/1000&&deadline->valuedouble<=epoch/1000+7*86400;
        if(resuming)valid=valid&&charge_plan.active&&vehicle_plan.active&&vehicle_plan.slot==index&&
            (settings.vehicle_fixed_profile||!vehicle_plan.card[0]||!strcmp(vehicle_plan.card,evcc_shell.session.card))&&
            (!vehicle_plan.identity[0]||!strcmp(vehicle_plan.identity,evcc_shell.session.identity));
        float maximum=vehicle_maximum_kw_locked(vehicles.rows[index].maximum_kw,charge_plan_maximum_kw(&settings,house_currents_ready_locked(now),evu_contact.stable));
        valid=valid&&maximum>0;
        if(valid){
            if(!resuming){
                vehicle_plan_clear_locked();soc_goal_clear(&soc_goal,manual);const vehicle_profile_t *p=&vehicles.rows[index];
                vehicle_plan=(vehicle_plan_t){.version=1,.active=true,.slot=index,.start_soc=soc,.target_soc=(float)target->valuedouble,.capacity_kwh=p->capacity_kwh,.efficiency=p->efficiency,.maximum_kw=p->maximum_kw,.last_soc=soc,.measured_soc=measured};
                snprintf(vehicle_plan.card,sizeof(vehicle_plan.card),"%s",settings.vehicle_fixed_profile?"":evcc_shell.session.card);snprintf(vehicle_plan.identity,sizeof(vehicle_plan.identity),"%s",evcc_shell.state=='C'?evcc_shell.session.identity:"");
                charge_plan=(charge_plan_t){.version=1,.active=true,.grid=manual,.target_wh=kwh*1000,.last_total_wh=energy.store.total_wh[0],.deadline_s=(int64_t)deadline->valuedouble};
            }
            {
                charge_plan.last_total_wh=energy.store.total_wh[0];vehicle_plan_reason[0]=0;vehicle_missing_since=0;
                restore_control_memory_locked(now,epoch,true);settings.enabled=true;settings.mode=charge_plan.grid?MODE_MANUAL:MODE_PV;
                if(soc_goal.manual&&resuming&&soc_goal.manual_current_a>0){settings.manual_current_a=soc_goal.manual_current_a;settings.manual_phases=soc_goal.manual_phases;}
                if(charge_plan.grid)vehicle_grid_current_locked(soc_goal.manual?fminf(maximum,settings.manual_current_a*ems_phase_voltage_sum(&settings,settings.manual_phases)/1000):maximum);
                if(soc_goal.manual){soc_goal.manual_current_a=settings.manual_current_a;soc_goal.manual_phases=settings.manual_phases;}
                plan_phase=charge_plan.grid?PLAN_GRID:PLAN_PV;plan_dirty=true;plan_revision++;memset(&charge_guard,0,sizeof(charge_guard));
                pv_control_step(&pv_control,false,0,active_min_a(),settings.max_charge_a,now);event_locked("plan_started");
            }
        }
    }
    UNLOCK();cJSON_Delete(o);
    if(!valid)return httpd_resp_send_err(req,HTTPD_400_BAD_REQUEST,problem?problem:"Fahrzeugprofil, aktuelle Shell-Daten und Ladeziel pruefen.");
    if(!save_plan()){LOCK();settings.enabled=false;charge_plan.active=false;vehicle_plan.active=false;plan_dirty=true;plan_revision++;UNLOCK();return httpd_resp_send_err(req,HTTPD_500_INTERNAL_SERVER_ERROR,"Plan nicht gespeichert; Ladung gestoppt.");}
    return send_json(req,vehicle_snapshot());
}
