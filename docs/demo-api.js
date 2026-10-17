(() => {
  const realFetch = window.fetch.bind(window);
  const now = new Date();
  const ymd = date => Number(`${date.getFullYear()}${String(date.getMonth()+1).padStart(2,'0')}${String(date.getDate()).padStart(2,'0')}`);
  const cfg = {
    phase_actuator_type:0,phase_feedback_count:1,phase_shelly_model:0,phase_shelly_pulse:false,phase_shelly_true_three_phase:false,phase_shelly_host:'',phase_shelly_password_set:false,shell_rs485_interface:0,meter_rs485_interface:0,house_rs485_interface:0,shell_rs485_host:'',meter_rs485_host:'',house_rs485_host:'',
    shell_rs485_interface:0,meter_rs485_interface:0,house_rs485_interface:0,shell_rs485_host:'',meter_rs485_host:'',house_rs485_host:'',
    basic_mode:true,expert_mode: false,control_status_visible:true,charge_plan_enabled:false,huawei_enabled:false,huawei_host:'',huawei_unit_id:1,huawei_battery:false,huawei_pv:false, wifi_ssid: 'TeeNet-Demo', wifi_password_set: true,
    mqtt_enabled: true, mqtt_uri: 'mqtt://broker.demo.local:1883', mqtt_username: 'demo',
    mqtt_password_set: true, mqtt_prefix: 'wallbox-ems', mqtt_input_source: 0,
    shell_limits_auto:false,shell_setup_host:'',max_charge_a:16,fixed_charge_phases:3, homeassistant_enabled: true, nominal_v: 230, price_kwh: .25, solar_price_kwh: .08,
    wallbox_meter_type: 'xemex', wallbox_meter_host: '', xemex_coils: 1,
    xemex_address: 1, meter_baud: 9600, meter_format: 1,
    zero_feed_enabled: true, house_meter_type: 'tasmota', house_meter_host: 'wattwaechter.demo.local',
    house_power_path: '', house_address: 1, house_meter_baud: 9600,
    house_meter_format: 0, house_xemex_coils: 3, zero_reserve_w: 0,
    pv_allocation_enabled:false,pv_priority:0,pv_house_priority_w:4000,pv_car_priority_w:6000,
    battery_protect: true, battery_reserve_soc: 40, battery_cloud_limit_w: 4000,
    battery_assist_limit_w: 4000, vehicle_soc_enabled: true, pv_display_enabled: true,
    relay_board_enabled: false, relay_active_low: false, relay1_mode: 0, relay2_mode: 0,
    external_mode_input_enabled: false, evu_input_enabled: false, evu_limit_a: 0,
    grid_limit_a:32,control_verified:true,grid_guard_enabled: false, phase_switch_enabled: false, phase_feedback_enabled:true,phase_feedback_closed_is_single:true,
    wallbox_tx_pin:17,wallbox_rx_pin:18,opendtu_prefix:'opendtu',opendtu_pv_topic:'opendtu/victron/123/PPV',opendtu_pv_valid_topic:'',opendtu_current_positive_discharge:false
  };
  const control = { mode: 'pv', enabled: true, current_a: 10, battery_use: true, battery_start_use: false };
  let huaweiProbe={host:'huawei.demo.local',unit_id:1};
  let totalKwh = 286.42;
  let rebootRequired = false;
  let scanFamily = 'shelly';
  let probe = { role: 'house', host: 'shelly.demo.local', type: 'shelly_gen2', path: '' };
  const started = Date.now();
  // Demo-only fault injection; no real device or network is accessed.
  const phaseFault = new URLSearchParams(window.location.search).get('house-phases');
  function housePhasesReady() {
    const supported=cfg.house_meter_type==='huawei'?cfg.huawei_enabled:
      ['xemex','sdm630','sdm630mct','shelly_gen2','em24_tcp'].includes(cfg.house_meter_type);
    return supported && !['missing','stale'].includes(phaseFault);
  }
  const gridFallback=()=>cfg.grid_guard_enabled&&!housePhasesReady();
  let plan={active:false,phase:'off',target_kwh:0,delivered_kwh:0,deadline_epoch:0};
  let planTick=Date.now();
  const events=[];
  function basicConfig(){
    if(!cfg.basic_mode)return;
    for(const name of ['pv_allocation_enabled','zero_feed_enabled','charge_plan_enabled','battery_protect','vehicle_soc_enabled','pv_display_enabled','relay_board_enabled','external_mode_input_enabled','evu_input_enabled','grid_guard_enabled','phase_switch_enabled','huawei_enabled','huawei_battery','huawei_pv'])cfg[name]=false;
    cfg.expert_mode=false;cfg.house_meter_type='tasmota';control.battery_use=control.battery_start_use=false;
    if(control.mode==='pv'||plan.active){control.enabled=false;control.mode='off';plan.active=false;plan.phase='off';}
  }

  function logEvent(reason){const m=measurements();events.unshift({sequence:events.length+1,reason,epoch:Math.floor(Date.now()/1000),uptime_s:Math.floor((Date.now()-started)/1000),target_kw:control.current_a*.23*phases(),actual_kw:m.wallbox/1000,house_kw:m.house/1000,battery_pct:73});if(events.length>48)events.pop();}

  const sessions = Array.from({length: 9}, (_, index) => {
    const date = new Date(now); date.setDate(now.getDate() - Math.floor(index / 2));
    date.setHours(8 + (index % 2) * 9, 15, 0, 0);
    const grid = 1.1 + (index % 3) * .7;
    const solar = 5.8 + (index % 4) * 1.15;
    const duration = 48 + (index % 4) * 13;
    return { id:index+1, date:ymd(date), start_epoch:Math.floor(date.getTime()/1000),
      end_epoch:Math.floor(date.getTime()/1000)+duration*60, charging_s:duration*60,
      energy_kwh:grid+solar, grid_kwh:grid, solar_kwh:solar, unknown_kwh:0,
      grid_cost_eur:grid*cfg.price_kwh, solar_cost_eur:solar*cfg.solar_price_kwh,
      cost_eur:grid*cfg.price_kwh+solar*cfg.solar_price_kwh, flags:0, active:false };
  });

  basicConfig();
  basicConfig();
  basicConfig();
  basicConfig();
  basicConfig();
  basicConfig();
  const phases=()=>cfg.phase_switch_enabled?(control.manual_phases??3):(cfg.fixed_charge_phases??3);
  function measurements() {
    const seconds = (Date.now() - started) / 1000;
    const pv = 8200+2400*Math.sin(seconds/17)+900*Math.sin(seconds/5.5);
    const base = 1450 + 260*Math.sin(seconds/11);
    let target = 0;
    if (control.enabled) {
      if (control.mode === 'manual') target = Math.max(0, control.current_a*230*phases());
      else if (control.mode === 'pv') {
        const budget=Math.max(0,pv-base);
        let allocation=budget+450;
        if(cfg.pv_allocation_enabled&&cfg.battery_protect)allocation=cfg.pv_priority===0?Math.max(0,budget-cfg.pv_house_priority_w):cfg.pv_priority===1?Math.min(budget,cfg.pv_car_priority_w):budget*cfg.pv_car_priority_w/(cfg.pv_house_priority_w+cfg.pv_car_priority_w);
        target=Math.max(0,Math.min(16*230*phases(),allocation));
      }
    }
    if (target > 0 && target < (phases()===1?1840:5400)) target = 0;
    if(gridFallback())target=Math.min(target,8000);
    const wallbox = target ? Math.max(0, Math.min(gridFallback()?8000:Infinity,target + 120*Math.sin(seconds/3))) : 0;
    const battery = cfg.battery_protect && control.battery_use && wallbox > pv-base ? Math.min(4000, wallbox-(pv-base)) : 0;
    const charge=cfg.battery_protect?Math.min(5000,Math.max(0,pv-base-wallbox)):0;
    const house = base + wallbox + charge - pv - battery;
    return {pv, base, wallbox, battery, charge, house, target};
  }

  function status() {
    const m = measurements();
    const stamp=Date.now();
    if(plan.active){
      if(control.enabled)plan.delivered_kwh=Math.min(plan.target_kwh,plan.delivered_kwh+m.wallbox*Math.max(0,stamp-planTick)/3600000000);
      if(plan.delivered_kwh>=plan.target_kwh){plan.active=false;plan.phase='complete';control.enabled=false;logEvent('plan_complete');}
      else if(stamp/1000>=plan.deadline_epoch){plan.active=false;plan.phase='expired';control.enabled=false;logEvent('plan_expired');}
      else if(!control.enabled)plan.phase='paused';
      else if(plan.phase==='grid'||(()=>{const duration=(plan.target_kwh-plan.delivered_kwh)/Math.min(gridFallback()?8:Infinity,16*230*(cfg.phase_switch_enabled?3:cfg.fixed_charge_phases)/1000)*3600;return plan.deadline_epoch-stamp/1000<=duration+Math.max(300,duration*.1);})()){plan.phase='grid';control.mode='manual';control.current_a=16;}
      else plan.phase='pv';
    }
    planTick=stamp;
    const amps = m.wallbox / (230*phases());
    const charging = m.wallbox > 300;
    const soc = 73-((Date.now()-started)/3600000)*2;
    return {
      huawei_enabled:cfg.huawei_enabled,huawei_ok:cfg.huawei_enabled,huawei_model:cfg.huawei_enabled?'SUN2000-8KTL-M1':'',battery_source:cfg.huawei_battery?'Huawei':'MQTT',pv_source:cfg.huawei_pv?'Huawei':'MQTT',
      version:'1.21',build_id:'demo-v1.21',charge_plan:{...plan},charge_plan_enabled:cfg.charge_plan_enabled, token:'demo-token', hostname:'TeeNet-Demo',
      ...control, mqtt_enabled:cfg.mqtt_enabled, mqtt_ok:true, homeassistant_enabled:cfg.homeassistant_enabled,
      wifi_ok:true, ap_active:false, ap_forced:false, wifi_fallback:false, station_ip:'teennet.demo.local',
      basic_mode:cfg.basic_mode,expert_mode:cfg.expert_mode, pv_display_enabled:cfg.pv_display_enabled,
      vehicle_soc_enabled:cfg.vehicle_soc_enabled, car_soc_pct:64,
      relay_board_enabled:cfg.relay_board_enabled, relay1_on:false, relay2_on:false,
      battery_protect:cfg.battery_protect, battery_ok:true,battery_soc_ok:true, battery_soc_pct:Math.max(0,soc),
      battery_reserve_soc:cfg.battery_reserve_soc, battery_capacity_kwh:20,
      battery_use:control.battery_use, battery_start_use:control.battery_start_use,
      battery_charge_w:m.charge,
      pv_allocation_enabled:cfg.pv_allocation_enabled,pv_priority:cfg.pv_priority,pv_allocation_ready:cfg.pv_allocation_enabled&&cfg.battery_protect,
      battery_discharge_w:m.battery, battery_discharge_limit_w:cfg.battery_assist_limit_w,
      battery_cloud_limit_w:cfg.battery_cloud_limit_w,
      shell_limits_auto:cfg.shell_limits_auto,shell_limits_ok:cfg.shell_limits_auto,shell_setup_host:cfg.shell_setup_host,phase_switch_enabled:cfg.phase_switch_enabled,phase_feedback_enabled:cfg.phase_feedback_enabled,phase_feedback_closed_is_single:cfg.phase_feedback_closed_is_single,phase_feedback:cfg.phase_switch_enabled&&cfg.phase_feedback_enabled?phases():0, fixed_charge_phases:cfg.fixed_charge_phases,charge_phases:phases(), manual_phases:phases(),
      phase_idle_inhibit:false, phase_state:'bereit',phase_wait_kind:'none',phase_wait_s:0,control_status_visible:cfg.control_status_visible, power_per_amp_kw:.23*phases(), single_power_per_amp_kw:.23,
      measurement_age_s:{wallbox:0,house:0,battery:0,pv:0},
      battery_buffer_remaining_s:0,diagnostic_storage_ok:true,hardware:'ESP32-S3 N16R8',ota_max_bytes:2097152,
      grid_price_kwh:cfg.price_kwh, solar_price_kwh:cfg.solar_price_kwh,
      target_current_a:m.target/(230*phases()), min_charge_a:8, max_charge_a:cfg.max_charge_a,
      charge_stop_latched:false, can_unlock:false,charge_retry_s:0, block_reason:!control.enabled?'off':!m.target?(control.mode==='pv'?'pv_waiting':'below_minimum'):gridFallback()?'grid_fallback':'none',
      pv_start_threshold_a:Math.min(cfg.max_charge_a, cfg.pv_allocation_enabled&&cfg.pv_priority===1&&cfg.pv_car_priority_w/(230*phases())>=8.69?Math.max(8.7,Math.min(9.7,cfg.pv_car_priority_w/(230*phases()))):9.7), pv_wait_s:0, pv_surplus_a:Math.max(0,(m.pv-m.base)/690), control_verified:cfg.control_verified!==false,
      meter_ok:true, wallbox_ok:true, feedback_ok:true, last_sent_age_ms:220,
      meter_a:[amps,amps,amps], actual_a:[amps,amps,amps], reported_a:phases()===1?[amps,0,0]:[amps,amps,amps],
      meter_total_a:amps*3, estimate_w:m.wallbox, wallbox_w:m.wallbox,
      xemex_coils:cfg.xemex_coils, wallbox_meter_type:cfg.wallbox_meter_type,
      wallbox_power_source:cfg.wallbox_meter_type, power_is_extrapolated:cfg.xemex_coils===1,
      house_meter_type:cfg.house_meter_type, zero_feed_enabled:cfg.zero_feed_enabled,
      house_power_w:m.house, house_meter_ok:true, house_power_ok:true,
      house_current_ok:housePhasesReady(),house_a:housePhasesReady()?[5.2,7.15,3.4]:[null,null,null], grid_guard_enabled:cfg.grid_guard_enabled, grid_guard_ok:!gridFallback(),grid_guard_fallback:gridFallback(),grid_guard_limit_kw:gridFallback()?8:null,
      pv_generation_w:m.pv, external_mode_input_enabled:cfg.external_mode_input_enabled,
      external_mode_contact:false, evu_input_enabled:cfg.evu_input_enabled, evu_active:false,
      evu_limit_a:cfg.evu_limit_a, clock_ok:true, current_date:ymd(now), storage_ok:true,
      uptime_s:Math.floor((Date.now()-started)/1000)+86400, free_heap:152800, reboot_required:rebootRequired,
      meter_failures:0, wallbox_requests:18922, mqtt_rejected:0, saved_ago_s:18,
      grid_limit_a:cfg.grid_limit_a, calibration_state:'stable', wallbox_rx_bytes:842120,
      wallbox_valid_frames:6309, wallbox_other_address:0, wallbox_crc_resyncs:2,
      wallbox_last_address:1, wallbox_last_function:3, wallbox_last_register:32,
      wallbox_last_count:2, wallbox_reply_age_s:1, wallbox_tx_errors:0,
      energy:[{total_kwh:totalKwh,boot_kwh:12.8,undated_kwh:0,today_kwh:14.72,
        today_covered_s:43200,today_charging_s:6920,today_cost:2.18}]
    };
  }

  function history() {
    const t = (Date.now()-started)/1000;
    return {
      points:Array.from({length:120},(_,i)=>({age_min:119-i,
        estimate_w:Math.max(0,6100+2100*Math.sin((i+t/60)/13)+520*Math.cos(i/4)),
        wallbox_w:Math.max(0,5700+1700*Math.sin((i+t/60)/15))})),
      days:Array.from({length:18},(_,i)=>{const d=new Date(now);d.setDate(now.getDate()-17+i);return {
        date:ymd(d),estimate_kwh:9+(i%6)*2.1,wallbox_kwh:4.5+(i%5)*1.3,
        estimate_covered_s:86000,wallbox_covered_s:82000};})
    };
  }

  function selectedSessions(url) {
    const params=url.searchParams,period=params.get('period'),from=params.get('from'),to=params.get('to');
    if(period&&!/^\d{4}(-\d{2})?$/.test(period))return [];
    if((from||to)&&(!/^\d{4}-\d{2}-\d{2}$/.test(from||'')||!/^\d{4}-\d{2}-\d{2}$/.test(to||'')||from>to))return [];
    return sessions.filter(s=>{
      const day=String(s.date);
      if(period)return day.startsWith(period.replace('-',''));
      if(from&&to)return day>=from.replaceAll('-','')&&day<=to.replaceAll('-','');
      return true;
    });
  }
  function sessionCsv(url) {
    const header='Datum;Energie_kWh;Netz_kWh;Netz_EUR;Solar_Akku_kWh;Solar_Akku_EUR;Kosten_EUR\n';
    return header+selectedSessions(url).map(s=>`${s.date};${s.energy_kwh.toFixed(3)};${s.grid_kwh.toFixed(3)};${s.grid_cost_eur.toFixed(2)};${s.solar_kwh.toFixed(3)};${s.solar_cost_eur.toFixed(2)};${s.cost_eur.toFixed(2)}`).join('\n');
  }
  function groupedSessions(url) {
    const params = url.searchParams;
    const offset = Number(params.get('offset')||0);
    const grouped = new Map();
    const selected=selectedSessions(url);
    selected.forEach(row => {
      if(!grouped.has(row.date)) grouped.set(row.date,{date:row.date,sessions:0,charging_s:0,energy_kwh:0,grid_kwh:0,solar_kwh:0,unknown_kwh:0,cost_eur:0,grid_cost_eur:0,solar_cost_eur:0,flags:0});
      const day=grouped.get(row.date);day.sessions++;
      ['charging_s','energy_kwh','grid_kwh','solar_kwh','unknown_kwh','cost_eur','grid_cost_eur','solar_cost_eur'].forEach(key=>day[key]+=row[key]);
    });
    const days=[...grouped.values()].sort((a,b)=>b.date-a.date);
    const sum=key=>selected.reduce((total,row)=>total+row[key],0);
    return {period:params.get('period')||'',count:days.length,offset,retained:sessions.length,
      days:days.slice(offset,offset+20),grid_kwh:sum('grid_kwh'),solar_kwh:sum('solar_kwh'),
      unknown_kwh:0,cost_eur:sum('cost_eur'),grid_cost_eur:sum('grid_cost_eur'),
      solar_cost_eur:sum('solar_cost_eur'),oldest_date:days.at(-1)?.date||0};
  }

  function json(data, statusCode=200) {
    return Promise.resolve(new Response(JSON.stringify(data),{status:statusCode,headers:{'Content-Type':'application/json'}}));
  }

  window.fetch = (input, options={}) => {
    const href = typeof input === 'string' ? input : input.url;
    const url = new URL(href, location.origin);
    if (!url.pathname.startsWith('/api/')) return realFetch(input, options);
    const body = options.body ? JSON.parse(options.body) : {};
    if (options.method === 'POST') {
      if(url.pathname==='/api/update/github')return json(body.action==='check'?{running:false,installing:false,ok:true,newer:false,version:status().version,message:'Demo: Die aktuelle Version wird angezeigt. Installation nur am echten Gerät.'}:{error:'Installation nur am echten TeeNet-Gerät.'},body.action==='check'?200:400);
      if (url.pathname === '/api/control') {if(cfg.basic_mode&&(body.mode==='pv'||body.mode==='grid_limit'||'pv_surplus_a' in body||body.battery_use||body.battery_start_use))return json({error:'Im Grundbetrieb nur manuell oder per MQTT laden.'},400);if(body.mode==='manual'&&control.mode==='pv'&&measurements().target>0&&!('current_a' in body))control.current_a=measurements().target/(230*phases());Object.assign(control, body);if(['enabled','mode','current_a'].some(key=>key in body)){plan.active=false;plan.phase='off';logEvent(control.enabled?'control_changed':'user_stop');}}
      else if (url.pathname === '/api/config') {
        const restartFields=["current_offset_a", "evu_input_enabled", "evu_input_pin", "external_mode_input_enabled", "external_mode_input_pin", "fixed_charge_phases", "house_address", "house_meter_baud", "house_meter_format", "house_meter_type", "house_rs485_host", "house_rs485_interface", "house_rts_pin", "house_rx_pin", "house_tx_pin", "house_xemex_coils", "meter_baud", "meter_format", "meter_rs485_host", "meter_rs485_interface", "min_charge_a", "mqtt_enabled", "mqtt_password", "mqtt_prefix", "mqtt_state_json", "mqtt_uri", "mqtt_username", "nominal_v", "phase_actuator_type", "phase_feedback_closed_is_single", "phase_feedback_count", "phase_feedback_enabled", "phase_feedback_pin", "phase_feedback_second_pin", "phase_mqtt_topic", "phase_output_host", "phase_output_password", "phase_output_switch_id", "phase_output_transport", "phase_shelly_host", "phase_shelly_model", "phase_shelly_password", "phase_shelly_pulse", "phase_shelly_true_three_phase", "phase_switch_enabled", "power_factor", "relay1_mode", "relay1_pin", "relay_active_low", "relay_board_enabled", "shell_rs485_host", "shell_rs485_interface", "status_led_pin", "version", "wallbox_address", "wallbox_meter_type", "wallbox_rts_pin", "wallbox_rx_pin", "wallbox_tx_pin", "wifi_password", "wifi_ssid", "xemex_address", "xemex_coils", "xemex_rts_pin", "xemex_rx_pin", "xemex_tx_pin"];
        if(cfg.mqtt_input_source===2||body.mqtt_input_source===2)restartFields.push(...["battery_protect", "mqtt_input_source", "opendtu_prefix", "opendtu_pv_topic", "opendtu_pv_valid_topic", "pv_display_enabled"]);
        const restartNeeded=restartFields.some(key=>key in body&&body[key]!==cfg[key]);
        rebootRequired ||= restartNeeded;
        if(rebootRequired){control.enabled=false;control.mode='off';}
        Object.assign(cfg, body);if(!cfg.battery_protect||!cfg.zero_feed_enabled)cfg.pv_allocation_enabled=false;basicConfig();if(!cfg.charge_plan_enabled&&plan.active){plan.active=false;plan.phase='off';control.enabled=false;control.mode='off';}logEvent('settings_saved');
        return json({ok:true,reboot_required:rebootRequired,restart_needed:restartNeeded,applied:!restartNeeded});
      }
      else if (url.pathname === '/api/reboot') rebootRequired=false;
      else if(url.pathname==='/api/plan'){
        if(body.active){
          if(cfg.basic_mode||!cfg.charge_plan_enabled)return json({error:'Ladeplan nicht aktiviert.'},400);
          if(!body.resume)plan={active:true,phase:'pv',target_kwh:body.kwh,delivered_kwh:0,deadline_epoch:body.deadline_epoch};
          plan.active=true;control.mode=plan.phase==='grid'?'manual':'pv';control.enabled=true;planTick=Date.now();logEvent('plan_started');
        }else {plan.active=false;plan.phase='off';control.enabled=false;control.mode='off';logEvent('plan_cancelled');}
      }
      else if(url.pathname==='/api/restore'){
        if(body.format!=='TeeNet-settings'||body.schema!==1||!body.settings)return json({error:'Keine passende Sicherung'},400);
        Object.assign(cfg,body.settings);basicConfig();cfg.control_verified=false;control.enabled=false;control.mode='off';plan.active=false;plan.phase='off';logEvent('settings_restored');
        return json({ok:true,reboot_required:true});
      }
      else if (url.pathname === '/api/huawei/preview') huaweiProbe={...huaweiProbe,...body};
      else if (url.pathname === '/api/meters/scan') scanFamily = body.family || 'shelly';
      else if (url.pathname === '/api/meters/preview') probe = {...probe,...body};
      else if (url.pathname === '/api/energy/reset') totalKwh = 0;
      else if (url.pathname === '/api/sessions/reset') sessions.length = 0;
      return json({ok:true,reboot_required:false});
    }
    if (url.pathname === '/api/status') return json(status());
    if(url.pathname==='/api/update/github')return json({running:false,installing:false,ok:true,newer:false,version:status().version,message:'Demo: Installation nur am echten Gerät.'});
    if (url.pathname === '/api/config') return json({...cfg});
    if(url.pathname==='/api/events')return json({events:[...events],persisted_faults:[]});
    if(url.pathname==='/api/backup'){
      const secrets=url.searchParams.get('secrets')==='1',settings={...cfg};
      delete settings.wifi_password_set;delete settings.mqtt_password_set;
      if(secrets){settings.wifi_password='demo-password';settings.mqtt_password='demo-password';}
      else {delete settings.wifi_password;delete settings.mqtt_password;}
      return json({format:'TeeNet-settings',schema:1,build_id:'demo-v1.21',includes_passwords:secrets,settings});
    }
    if (url.pathname === '/api/history') return json(history());
    if (url.pathname === '/api/sessions') return json(groupedSessions(url));
    if (url.pathname === '/api/export.csv') return Promise.resolve(new Response(sessionCsv(url),{headers:{'Content-Type':'text/csv'}}));
    if (url.pathname === '/api/sessions/day') {
      const date=Number(url.searchParams.get('date')||0),offset=Number(url.searchParams.get('offset')||0);
      const rows=sessions.filter(row=>row.date===date);
      return json({date,offset,count:rows.length,sessions:rows.slice(offset,offset+20)});
    }
    if(url.pathname==='/api/shell/scan')return json({running:false,partial:false,progress:100,devices:[{host:'shell.demo.local',grid_limit_a:32,max_charge_a:16}]});
    if(url.pathname==='/api/huawei/scan')return json({running:false,partial:false,progress:100,devices:[{host:'huawei.demo.local',unit_id:1,model:'SUN2000-8KTL-M1'}]});
    if(url.pathname==='/api/huawei/preview')return json({...huaweiProbe,running:false,connected:true,model:'SUN2000-8KTL-M1',house_power_w:-1840,house_current_ok:true,house_a:[5.2,7.15,3.4],pv_generation_w:8120,battery_soc_pct:73,battery_charge_w:0,battery_discharge_w:1200});
    if (url.pathname === '/api/wifi/scan') return json({networks:[{ssid:'TeeNet-Demo',rssi:-38},{ssid:'PV-Anlage',rssi:-61},{ssid:'Gastnetz',rssi:-73}]});
    if (url.pathname === '/api/meters/scan') return json({running:false,partial:false,progress:100,family:scanFamily,meters:scanFamily==='tasmota'?[{host:'wattwaechter.demo.local',type:'tasmota',watts:-1840,age_ms:280}]:[{host:'shelly.demo.local',type:'shelly_gen2',watts:-1840,age_ms:190}]});
    if (url.pathname === '/api/meters/preview') return json({...probe,running:false,ok:true,watts:probe.role==='wallbox'?measurements().wallbox:-1840,age_ms:160});
    return json({error:'In der Demo nicht verfügbar'},404);
  };

  document.addEventListener('DOMContentLoaded', () => {
    const csv = document.getElementById('session-csv');
    if (csv) csv.addEventListener('click', event => {
      event.preventDefault();
      if(csv.getAttribute('aria-disabled')==='true')return;
      const url=new URL(csv.href,location.origin);
      const link=document.createElement('a');link.href=URL.createObjectURL(new Blob([sessionCsv(url)],{type:'text/csv'}));link.download='teennet-demo-ladesitzungen.csv';link.click();setTimeout(()=>URL.revokeObjectURL(link.href),1000);
    });
  });
})();

