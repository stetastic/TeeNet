/* Optional profiles have their own NVS keys; existing device configuration is unchanged. */
static vehicle_store_t vehicles={.version=1};
static vehicle_plan_t vehicle_plan;
static uint32_t vehicle_revision=1;
static float vehicle_soc[VEHICLE_COUNT];
static int64_t vehicle_soc_at[VEHICLE_COUNT];
static int64_t vehicle_missing_since;
static char vehicle_plan_reason[32];
static void vehicle_load(void){
    nvs_handle_t h;size_t n=sizeof(vehicles);
    if(config_storage_ok&&nvs_open("teenet_vehicles",NVS_READONLY,&h)==ESP_OK){
        esp_err_t e=nvs_get_blob(h,"profiles",&vehicles,&n);nvs_close(h);
        if(e!=ESP_OK||n!=sizeof(vehicles)||!vehicle_store_valid(&vehicles))vehicles=(vehicle_store_t){.version=1};
    }
}
static bool vehicle_soc_fresh_locked(unsigned slot,int64_t now){
    if(slot>=VEHICLE_COUNT||!mqtt_online||!vehicles.rows[slot].soc_source)return false;
    return vehicles.rows[slot].soc_source==2?settings.vehicle_soc_enabled&&fresh(now,car_soc_at,ems_param_ms(EP_CAR_TTL)):fresh(now,vehicle_soc_at[slot],ems_param_ms(EP_CAR_TTL));
}
static float vehicle_soc_value_locked(unsigned slot){return vehicles.rows[slot].soc_source==2?car_soc:vehicle_soc[slot];}
static bool vehicle_soc_message_locked(const char *suffix,const char *payload){
    for(unsigned i=0;i<VEHICLE_COUNT;i++){
        char name[40];snprintf(name,sizeof(name),"input/vehicle_%u_soc_pct",i+1);
        if(strcmp(name,suffix))continue;
        char *end=NULL;errno=0;double v=strtod(payload,&end);
        bool valid=payload[0]&&end&&!*end&&!errno&&isfinite(v)&&v>=0&&v<=100;
        if(vehicles.enabled&&vehicles.rows[i].name[0]&&vehicles.rows[i].soc_source==1&&valid){vehicle_soc[i]=(float)v;vehicle_soc_at[i]=now_ms();}
        else vehicle_soc_at[i]=0;
        return true; /* Invalid/unavailable explicitly invalidates this vehicle only. */
    }
    return false;
}
static bool vehicle_http_fresh_locked(int64_t now){return evcc_shell_at>0&&now>=evcc_shell_at&&now-evcc_shell_at<EVCC_SHELL_TTL_MS;}
static int vehicle_current_locked(int64_t now){
    if(!vehicles.enabled)return -1;
    unsigned fixed=settings.vehicle_fixed_profile;
    if(fixed)return fixed<=VEHICLE_COUNT&&vehicles.rows[fixed-1].name[0]?(int)fixed-1:-1;
    return vehicle_http_fresh_locked(now)?vehicle_match(&vehicles,evcc_shell.session.card):-1;
}
/* Session-local estimate. Never written repeatedly to flash or used as a measured SoC.
   A reboot discards it; the next manual start can request a new start value. */
static struct {bool active,fixed;unsigned slot;uint32_t revision;float start,capacity,efficiency;double anchor_wh;
    char card[VEHICLE_CARD_BYTES],identity[65];} vehicle_estimate;
static bool vehicle_estimate_soc_locked(unsigned slot,int64_t now,float *soc){
    return vehicle_estimate.active&&vehicle_estimate.slot==slot&&vehicle_estimate.revision==vehicle_revision&&
        vehicle_estimate.fixed==(settings.vehicle_fixed_profile!=0)&&vehicle_current_locked(now)==(int)slot&&
        (vehicle_estimate.fixed||vehicle_http_fresh_locked(now))&&
        (!vehicle_http_fresh_locked(now)||
            ((evcc_shell.state=='B'||evcc_shell.state=='C'||evcc_shell.state=='D')&&
             (vehicle_estimate.fixed||(evcc_shell.session.present&&!strcmp(vehicle_estimate.card,evcc_shell.session.card)))&&
             (!vehicle_estimate.identity[0]||!evcc_shell.session.identity[0]||!strcmp(vehicle_estimate.identity,evcc_shell.session.identity))))&&
        vehicle_estimated_soc(vehicle_estimate.start,vehicle_estimate.capacity,vehicle_estimate.efficiency,
            vehicle_estimate.anchor_wh,energy.store.total_wh[0],soc);
}
static bool vehicle_estimate_begin_locked(unsigned slot,float soc,int64_t now){
    if(slot>=VEHICLE_COUNT||!vehicles.enabled||vehicle_current_locked(now)!=(int)slot||
       !isfinite(soc)||soc<0||soc>100||!vehicles.rows[slot].name[0])return false;
    if(!settings.vehicle_fixed_profile&&(!evcc_shell.session.present||(evcc_shell.state!='B'&&evcc_shell.state!='C')))return false;
    if(vehicle_http_fresh_locked(now)&&evcc_shell.state!='B'&&evcc_shell.state!='C')return false;
    vehicle_estimate.active=true;vehicle_estimate.slot=slot;vehicle_estimate.revision=vehicle_revision;
    vehicle_estimate.fixed=settings.vehicle_fixed_profile!=0;
    vehicle_estimate.start=soc;vehicle_estimate.capacity=vehicles.rows[slot].capacity_kwh;
    vehicle_estimate.efficiency=vehicles.rows[slot].efficiency;vehicle_estimate.anchor_wh=energy.store.total_wh[0];
    snprintf(vehicle_estimate.card,sizeof(vehicle_estimate.card),"%s",evcc_shell.session.card);
    snprintf(vehicle_estimate.identity,sizeof(vehicle_estimate.identity),"%s",vehicle_http_fresh_locked(now)&&evcc_shell.state=='C'?evcc_shell.session.identity:"");return true;
}
static void vehicle_estimate_step_locked(int64_t now){
    if(!vehicle_estimate.active)return;
    unsigned slot=vehicle_estimate.slot;float soc;
    if(!vehicle_estimate_soc_locked(slot,now,&soc)){vehicle_estimate.active=false;return;}
    /* State B can still expose the previous transaction. Bind the first actual charge. */
    if(!vehicle_estimate.identity[0]&&vehicle_http_fresh_locked(now)&&evcc_shell.state=='C')
        snprintf(vehicle_estimate.identity,sizeof(vehicle_estimate.identity),"%s",evcc_shell.session.identity);
    if(vehicle_soc_fresh_locked(slot,now)){
        float measured=vehicle_soc_value_locked(slot);
        if(fabsf(measured-vehicle_estimate.start)>=.1f){vehicle_estimate.start=measured;vehicle_estimate.anchor_wh=energy.store.total_wh[0];}
    }
}
static float vehicle_maximum_kw_locked(float configured,float maximum){
    float limit=fminf(maximum,configured);
    unsigned phases=settings.fixed_charge_phases;
    if(settings.phase_switch_enabled)phases=limit*1000<settings.min_charge_a*ems_phase_voltage_sum(&settings,3)?1:3;
    limit=fminf(limit,settings.max_charge_a*ems_phase_voltage_sum(&settings,phases)/1000);
    float minimum=settings.min_charge_a*ems_phase_voltage_sum(&settings,phases)/1000;
    return limit+.001f<minimum?0:limit;
}
static float vehicle_plan_maximum_locked(float maximum){return vehicle_plan.active?vehicle_maximum_kw_locked(vehicle_plan.maximum_kw,maximum):maximum;}
static float vehicle_limit_current_locked(float current){
    if(!vehicle_plan.active||!charge_plan.active)return current;
    float limit=vehicle_plan.maximum_kw*1000/ems_phase_voltage_sum(&settings,settings.charge_phases);
    current=fminf(current,limit);return current<settings.min_charge_a?0:current;
}
static float vehicle_phase_surplus_locked(float watts){return vehicle_plan.active&&charge_plan.active?fminf(watts,vehicle_plan.maximum_kw*1000):watts;}
static void vehicle_grid_current_locked(float maximum){
    unsigned phases=settings.phase_switch_enabled?(maximum*1000<settings.min_charge_a*ems_phase_voltage_sum(&settings,3)?1:3):settings.fixed_charge_phases;
    settings.manual_phases=(uint8_t)phases;
    settings.manual_current_a=fminf(settings.max_charge_a,maximum*1000/ems_phase_voltage_sum(&settings,phases));
}
static void vehicle_plan_clear_locked(void){memset(&vehicle_plan,0,sizeof(vehicle_plan));vehicle_missing_since=0;vehicle_plan_reason[0]=0;}
static void vehicle_pause_locked(const char *reason,bool cancel){
    settings.enabled=false;plan_phase=PLAN_PAUSED;
    if(cancel){charge_plan.active=false;vehicle_plan.active=false;settings.mode=MODE_OFF;}
    snprintf(vehicle_plan_reason,sizeof(vehicle_plan_reason),"%s",reason);plan_dirty=true;plan_revision++;
}
/* Called under the existing state lock, before the ordinary deadline planner. */
static void vehicle_plan_step_locked(int64_t now){
    if(!vehicle_plan.active)return;
    if(!charge_plan.active){vehicle_plan.active=false;return;}
    if(!vehicles.enabled||vehicle_plan.slot>=VEHICLE_COUNT){vehicle_pause_locked("profiles_disabled",true);return;}
    if(!vehicle_http_fresh_locked(now)){if(settings.enabled)vehicle_pause_locked("wallbox_unavailable",false);return;}
    const vehicle_session_t *s=&evcc_shell.session;
    if(vehicle_plan.identity[0]&&s->identity[0]&&strcmp(s->identity,vehicle_plan.identity)){vehicle_pause_locked("vehicle_changed",true);return;}
    if((!settings.vehicle_fixed_profile&&!s->present)||evcc_shell.state=='A'||evcc_shell.state=='D'){
        if(!vehicle_missing_since)vehicle_missing_since=now;
        if(now-vehicle_missing_since>=15000)vehicle_pause_locked("vehicle_disconnected",true);
        else if(settings.enabled)vehicle_pause_locked("vehicle_card_missing",false);
        return;
    }
    if(evcc_shell.state!='B'&&evcc_shell.state!='C'){if(settings.enabled)vehicle_pause_locked("vehicle_not_ready",false);return;}
    if(settings.vehicle_fixed_profile){
        if(vehicle_current_locked(now)!=vehicle_plan.slot){vehicle_pause_locked("vehicle_changed",true);return;}
        if(!vehicle_plan.identity[0]&&evcc_shell.state=='C'&&s->identity[0])snprintf(vehicle_plan.identity,sizeof(vehicle_plan.identity),"%s",s->identity);
        vehicle_missing_since=0;
    }else if(s->card[0]){
        int slot=vehicle_match(&vehicles,s->card);
        if(slot!=vehicle_plan.slot||(vehicle_plan.card[0]&&strcmp(s->card,vehicle_plan.card))){vehicle_pause_locked("vehicle_changed",true);return;}
        if(!vehicle_plan.card[0])snprintf(vehicle_plan.card,sizeof(vehicle_plan.card),"%s",s->card);
        if(!vehicle_plan.identity[0]&&evcc_shell.state=='C'&&s->identity[0])snprintf(vehicle_plan.identity,sizeof(vehicle_plan.identity),"%s",s->identity);
        vehicle_missing_since=0;
    }else {if(settings.enabled)vehicle_pause_locked("vehicle_card_missing",false);return;}
    if(!settings.enabled)return; /* Reboot, stop and failed identity checks never auto-resume. */
    if(vehicle_soc_fresh_locked(vehicle_plan.slot,now)){
        float soc=vehicle_soc_value_locked(vehicle_plan.slot);
        if(soc>=vehicle_plan.target_soc){charge_plan.delivered_wh=charge_plan.target_wh;return;}
        if(fabsf(soc-vehicle_plan.last_soc)>=.1f){
            /* Anchor each changed measured SoC at the current energy counter. No double counting. */
            float remaining=vehicle_plan.capacity_kwh*(vehicle_plan.target_soc-soc)/100/vehicle_plan.efficiency;
            charge_plan.target_wh=charge_plan.delivered_wh+remaining*1000;
            charge_plan.last_total_wh=energy.store.total_wh[0];vehicle_plan.last_soc=soc;vehicle_plan.anchor_wh=charge_plan.delivered_wh;vehicle_plan.measured_soc=true;
            plan_dirty=true;plan_revision++;
        }
    }
}
