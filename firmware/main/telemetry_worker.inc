/* Optional HTTPS work runs in its own task only while a report or deletion is due. */
#define TELEMETRY_STACK_BYTES 6144u
static bool telemetry_worker_running;

static bool telemetry_ready(size_t stack_reserve){
    LOCK();int64_t now=now_ms();
    bool due=telemetry_due(&telemetry_config,terms_accepted,wifi_online,
        ota_in_progress||restarting||backup_in_progress||boot_validation_pending,now,telemetry_next_at);
    if(!due){UNLOCK();return false;}
    bool requested=settings.enabled||evcc_control.enabled;
    bool shell_known=shell_vehicle_fresh(&evcc_shell,evcc_shell_at,now);
    telemetry_runtime_t runtime={
        .charging_requested=requested,
        .long_meter_idle=telemetry_long_idle(telemetry_meter_idle,actual_at,requested),
        .long_http_idle=settings.shell_http_control_enabled&&shell_known&&
            telemetry_long_idle(telemetry_shell_idle,evcc_shell_at,requested),
        .phase_busy=phase_config_saving||(settings.phase_switch_enabled&&
            (phase_startup||phase_state!=PHASE_READY||phase_shelly_pending||phase_shelly_inflight||
             phase_shelly_rechecking||phase_mqtt_pending||phase_mqtt_inflight)),
        .test_active=phase_test_active,
        .current_fresh=fresh(now,actual_at,wallbox_meter_ttl()),
        .shell_configured=evcc_control.configured||settings.shell_limits_auto||
            settings.shell_setup_host[0]||shell_active_host[0],
        .shell_known=shell_known,
        .session_present=evcc_shell.session.present,.vehicle_state=evcc_shell.state,
        .shell_reports_idle=!strcmp(evcc_shell.raw,"idle"),
        .current_a={actual_a[0],actual_a[1],actual_a[2]}};
#if CONFIG_TEE_HTTP_CONTROL_TRIAL
    runtime.test_active=runtime.test_active||normal_offset_trial.armed||shell_http_trial.armed;
#endif
    UNLOCK();
    runtime.internal_free=heap_caps_get_free_size(MALLOC_CAP_INTERNAL|MALLOC_CAP_8BIT);
    runtime.internal_largest=heap_caps_get_largest_free_block(MALLOC_CAP_INTERNAL|MALLOC_CAP_8BIT);
    runtime.internal_free=runtime.internal_free>=stack_reserve?runtime.internal_free-stack_reserve:0;
    const char *reason=telemetry_runtime_block(&runtime);
    if(!reason&&time(NULL)<1735689600)reason="clock";
    LOCK();telemetry_wait_reason=reason;UNLOCK();
    return reason==NULL;
}

/* Caller holds telemetry_lock, which serializes sending with consent changes. */
static void telemetry_send_due(void){
    int64_t started=now_ms();
    LOCK();telemetry_config_t c=telemetry_config;UNLOCK();
    char body[288],auth[73];
    if(c.delete_pending)snprintf(body,sizeof(body),"{\"id\":\"%s\"}",c.id);
    else snprintf(body,sizeof(body),"{\"id\":\"%s\",\"version\":\"%s\",\"build\":\"%s\",\"hardware\":\"%s\",\"consent_revision\":\"%s\"}",c.id,VERSION,EMS_BUILD_ID,EMS_HARDWARE_ID,TELEMETRY_REVISION);
    snprintf(auth,sizeof(auth),"Bearer %s",c.key);
    esp_http_client_config_t cfg={.url=TELEMETRY_ENDPOINT,.method=c.delete_pending?HTTP_METHOD_DELETE:HTTP_METHOD_POST,
        .timeout_ms=5000,.crt_bundle_attach=esp_crt_bundle_attach,.disable_auto_redirect=true,.buffer_size=512,.buffer_size_tx=512};
    esp_http_client_handle_t client=esp_http_client_init(&cfg);bool ok=false;
    int status=0;
    if(client){
        esp_http_client_set_header(client,"Content-Type","application/json");
        esp_http_client_set_header(client,"Authorization",auth);
        esp_http_client_set_header(client,"User-Agent","TeeNet-installation-statistics");
        esp_http_client_set_post_field(client,body,strlen(body));
        esp_err_t err=esp_http_client_perform(client);status=esp_http_client_get_status_code(client);
        ok=err==ESP_OK&&status>=200&&status<300;
        esp_http_client_cleanup(client);
    }
    memset(auth,0,sizeof(auth));
    if(ok&&c.delete_pending){
        c.delete_pending=0;memset(c.id,0,sizeof(c.id));memset(c.key,0,sizeof(c.key));
        if(telemetry_store(&c)){LOCK();telemetry_config=c;UNLOCK();}
    }
    LOCK();telemetry_last_ok=ok;telemetry_last_http=status;telemetry_duration_ms=(uint32_t)(now_ms()-started);
    telemetry_wait_reason=ok?NULL:status>0?"server":"connection";if(ok)telemetry_last_at=now_ms();
    telemetry_next_at=now_ms()+TELEMETRY_INTERVAL_MS+(esp_random()%15000);UNLOCK();
    memset(&c,0,sizeof(c));
}
static void telemetry_task(void *arg){
    (void)arg;
    if(xSemaphoreTake(telemetry_lock,0)==pdTRUE){
        /* Recheck consent, current, time and actual RAM after task allocation. */
        if(telemetry_ready(0))telemetry_send_due();
        xSemaphoreGive(telemetry_lock);
    }
    LOCK();telemetry_worker_running=false;UNLOCK();
    vTaskDelete(NULL);
}
/* Only status_task schedules the sender. A busy or failed start stays due. */
static void telemetry_schedule(void){
    if(!TELEMETRY_ENDPOINT[0]||!telemetry_lock)return;
    LOCK();bool running=telemetry_worker_running;UNLOCK();
    if(running||!telemetry_ready(TELEMETRY_STACK_BYTES))return;
    LOCK();telemetry_worker_running=true;UNLOCK();
    if(xTaskCreate(telemetry_task,"device_stats",TELEMETRY_STACK_BYTES,NULL,2,NULL)!=pdPASS){
        LOCK();telemetry_worker_running=false;UNLOCK();
    }
}
