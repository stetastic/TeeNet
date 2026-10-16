'use strict';

const $=id=>document.getElementById(id);

const format=(value,digits=2)=>typeof value==='number'&&Number.isFinite(value)?value.toLocaleString('de-DE',{minimumFractionDigits:digits,maximumFractionDigits:digits}):'—';

function setTheme(theme){
  const selected=theme==='dark'?'dark':'light';document.documentElement.dataset.theme=selected;
  try{localStorage.setItem('teennet-theme',selected);}catch(error){}
  $('theme-light')?.setAttribute('aria-pressed',String(selected==='light'));
  $('theme-dark')?.setAttribute('aria-pressed',String(selected==='dark'));
  document.querySelector('meta[name="theme-color"]')?.setAttribute('content',selected==='dark'?'#0e1821':'#f2f7fa');
  drawCharts();
}

async function loadDeviceTheme(){
  try{
    const result=await api('/api/theme',undefined,4000);
    if(result?.theme==='dark'||result?.theme==='light')setTheme(result.theme);
  }catch(error){}
}

async function selectTheme(theme){
  setTheme(theme);
  try{await api('/api/theme',{theme},5000);}catch(error){
    /* The browser preference remains useful while the controller is offline. */
  }
}

const reasons={firmware_check:'Firmware wird geprüft · Ladung gesperrt.',firmware_failed:'Firmware-Prüfung fehlgeschlagen · Ladung gesperrt.',maintenance:'Wartung läuft · Ladung gesperrt.',reboot:'Gespeichert · Neustart erforderlich.',unsupported_meter:'Zählerprofil nicht unterstützt · Regelung gesperrt.',house_meter:'Hauszählerdaten fehlen · PV-Laden pausiert.',commissioning:'Einrichtung läuft · Regelung bis zur Prüfung an der Wallbox gesperrt.',uart:'RS485-Schnittstelle nicht bereit · Regelung gesperrt.',meter:'Wallbox-Messwert fehlt · Laden pausiert.',feedback:'Wallbox-Strommessung fehlt · Regelung gesperrt.',off:'Laderegelung ausgeschaltet.',pv_stale:'PV-Sollwert veraltet · Regelung gesperrt.',below_minimum:'Sollwert unter Mindeststrom · Stop-Anforderung aktiv.',none:'Regelung aktiv · frische Messwerte vorhanden.'};
Object.assign(reasons,{evu_stop:'EVU-Kontakt aktiv · Ladung gestoppt.',grid_fallback:'Haus-Phasenwerte fehlen oder sind veraltet · Laden bis 8 kW.'});

Object.assign(reasons,{wallbox:'Keine aktuelle Wallbox-Abfrage · PV-Regelung gesperrt.',pv_waiting:'PV wartet auf ausreichend Überschuss.',pv_starting:'Überschuss wird vor dem Start geprüft.',pv_stopping:'PV-Schwankung überbrücken; bei weiterem Mangel stoppen.',pv_cooldown:'Pause vor erneutem PV-Start.'});

let state=null,history={points:[],days:[]},token='',online=false,dirty=false,batterySliderDirty=false,initialized=false,busy=false,configSaving=false,settingsRebootPending=false,settingsRebootDeferred=false,source=0,toastTimer,uploading=false,powerQueued=false,reserveQueued=false,historyBusy=false,statusFailures=0;

// A stopped session has mode "off". Keep its next selection in this browser
// without sending a control command or starting a charge.
let preferredChargeMode=null;
try{const saved=localStorage.getItem('teennet-charge-mode');if(['manual','pv'].includes(saved))preferredChargeMode=saved;}catch(error){}
function rememberChargeMode(mode){
  if(!['manual','pv'].includes(mode)||preferredChargeMode===mode)return;
  preferredChargeMode=mode;
  try{localStorage.setItem('teennet-charge-mode',mode);}catch(error){}
}
function displayedChargeMode(s){
  if(s.basic_mode||s.zero_feed_enabled===false)return 'manual';
  if(s.charge_plan_enabled&&s.charge_plan?.active)return 'pv';
  if(s.external_mode_input_enabled)return s.external_mode_contact?'pv':'manual';
  if(s.enabled&&['manual','pv'].includes(s.mode))return s.mode;
  return preferredChargeMode||(['manual','pv'].includes(s.mode)?s.mode:'manual');
}

reasons.charge_ended='Mehrere Ladeabbrüche in kurzer Zeit. Bitte Ursache prüfen und erneut starten.';
reasons.charge_retry='Ladeabbruch · warte auf neuen Startversuch.';

reasons.battery_stale='Akku-Ladestand fehlt · Akkuhilfe pausiert.';

reasons.battery_reserve='Entladegrenze erreicht · warte auf PV-Überschuss.';
reasons.battery_power_stale='Akku-Leistung fehlt · Akkuhilfe pausiert.';
reasons.battery_no_response='Hausakku liefert nicht genug Leistung · warte auf PV-Überschuss.';

let configBaseline={};
let configSaveError='';
let meterScanning=false,meterResults=[];
let scanFamily='',scanTarget='house',previewRunning=false,previewRole='house',previewTurn=0;
const previewKeys={house:'',wallbox:''},previewTimes={house:0,wallbox:0};
function meterFamily(role='house'){return role==='house'&&$('config-form').elements.house_meter_type.value==='tasmota'?'tasmota':'shelly';}
function previewRequest(role='house'){const f=$('config-form').elements;return role==='wallbox'?{role,host:f.wallbox_meter_host.value.trim(),type:f.wallbox_meter_type.value,path:'',unit_id:f.wallbox_meter_type.value==='em24_tcp'?Number(f.xemex_address.value):1}:{role,host:f.house_meter_host.value.trim(),type:f.house_meter_type.value,path:f.house_meter_type.value==='em24_tcp'?'':f.house_power_path.value.trim(),unit_id:f.house_meter_type.value==='em24_tcp'?Number(f.house_address.value):1};}
function scanUi(role){return role==='wallbox'?{button:$('wallbox-meter-scan'),status:$('wallbox-meter-scan-status'),select:$('wallbox-meter-found')}:{button:$('meter-scan'),status:$('meter-scan-status'),select:$('meter-found')};}
function previewUi(role){return role==='wallbox'?{box:$('wallbox-preview'),host:$('wallbox-preview-host'),value:$('wallbox-preview-value'),state:$('wallbox-preview-state')}:{box:$('house-preview'),host:$('house-preview-host'),value:$('house-preview-value'),state:$('house-preview-state')};}

reasons.manual_stop='Sollwert unter Mindeststrom · Stop angefordert.';
reasons.phase_fault='Phasenumschaltung nicht bereit · Ladung gesperrt. Konfiguration und Rückmeldung prüfen.';
reasons.phase_test='Schütztest läuft · Ladung bleibt gestoppt.';
reasons.phase_switching='Phasenwechsel läuft · Ladung wird vor dem Schalten gestoppt.';
reasons.phase_idle='Kein Ladestrom · Schütz aus. Bei Bedarf erneut starten.';
reasons.evcc_fault='Shell meldet einen Fehler · evcc-Ladung pausiert.';
reasons.evcc_timeout='evcc-Verbindung ausgefallen · Ladung gestoppt.';




function toast(text,error=false){$('toast').textContent=text;$('toast').classList.toggle('error',error);$('toast').hidden=false;clearTimeout(toastTimer);toastTimer=setTimeout(()=>$('toast').hidden=true,6500);}

async function api(path,body,timeoutMs=8000,auth=false){const controller=new AbortController(),timer=setTimeout(()=>controller.abort(),timeoutMs);try{const response=await fetch(path,{signal:controller.signal,cache:'no-store',...(auth?{headers:{'X-EMS-Token':token}}:{}),...(body===undefined?{}:{method:'POST',headers:{'Content-Type':'application/json','X-EMS-Token':token},body:JSON.stringify(body)})});if(!response.ok)throw new Error((await response.text()).slice(0,240)||`HTTP ${response.status}`);return await response.json();}finally{clearTimeout(timer);}}

function writes(){const batteryInactive=$('mode').value!=='pv';document.querySelectorAll('#config-form input, #config-form select, #show-wifi-password, #show-mqtt-password').forEach(el=>el.disabled=!online||!initialized||configSaving||!!state?.firmware_check_pending);document.querySelectorAll('[data-write]').forEach(b=>b.disabled=!online||busy||!!state?.firmware_check_pending||(!initialized&&!!b.closest('#config-form'))||((b.id==='battery-toggle'||b.id==='battery-start-toggle')&&(batteryInactive||!state?.battery_protect)));$('current').disabled=!online||busy||!!state?.firmware_check_pending;$('battery-reserve-slider').disabled=!online||busy||batteryInactive||!state?.battery_protect;for(const role of ['house','wallbox'])scanUi(role).button.disabled=!online||busy||!initialized||meterScanning||previewRunning||!!window.teennetFeatures?.networkBusy();window.teennetFeatures?.updateWrites();if($('config-form').elements.basic_mode?.value==='true')document.querySelectorAll('#house-settings input,#house-settings select,#battery-settings input,#hardware-settings input,#relay-settings select,#huawei-settings input').forEach(el=>el.disabled=true);window.teenetEvccTest?.updateWrites(state);window.teenetTerms?.updateWrites();window.teenetExpert?.updateWrites();window.teenetShellyPhase?.updateLive();window.teenetPhaseTest?.update();}

function showConnection(ok){online=ok;writes();}

function diagnostic(label,value){const box=document.createElement('div');box.textContent=label;const strong=document.createElement('b');strong.textContent=value;box.append(strong);return box;}

function controlStatus(s,valid=true){
  const help={meter:'#meter-settings',feedback:'#meter-settings',wallbox:'#shell-settings',uart:'#shell-settings',house_meter:'#house-settings',battery_stale:'#mqtt-settings',phase_fault:'#relay-settings',charge_ended:'#diagnostic-settings',reboot:'#maintenance'};
  const result=(text,kind='normal')=>({text,kind,help:!valid?'#connection-settings':help[s.block_reason]||((s.phase_state==='fehler'||s.charge_stop_latched)?'#diagnostic-settings':null)});
  const wait=(text,seconds,maximum=false)=>text+(Number.isFinite(seconds)&&seconds>0?` · ${maximum?'höchstens noch':'noch'} ${Math.ceil(seconds)} s`:'');
  if(!valid)return result('Verbindung unterbrochen · Status nicht aktuell','fault');
  if(s.evcc_test_enabled){
    if(s.evcc_local_stop)return result('evcc · lokal gestoppt; Freigabe über den Ladeknopf','waiting');
    if(s.evcc_lease_expired)return result('evcc · Verbindung ausgefallen; Stopp angefordert','fault');
    if(!s.evcc_status_known)return result('evcc · Fahrzeugstatus unbekannt; Ladung pausiert','waiting');
  }
  if(s.phase_state==='fehler')return result('Phasenumschaltung gestört · Ladung gesperrt','fault');
  const phaseWait={connection:'Phasenumschaltung · Verbindung wird aufgebaut',current_zero:'Phasenwechsel · warte auf Ladestillstand',zero_hold:'Phasenwechsel · stromlose Wartezeit',contact:'Phasenwechsel · warte auf Schütz-Rückmeldung',motion:'Phasenwechsel · Schütz schaltet',settle:'Phasenwechsel · Stabilisierung',fresh_data:'Phasenwechsel · warte auf frische Messwerte'};
  if(s.phase_switch_enabled&&s.phase_state!=='bereit')return result(wait(phaseWait[s.phase_wait_kind]||'Phasenwechsel läuft',s.phase_wait_s,['connection','current_zero','contact','fresh_data'].includes(s.phase_wait_kind)),'waiting');
  if(s.charge_stop_latched)return result('Mehrere Ladeabbrüche · Start gesperrt','fault');
  if(s.charge_retry_s>0)return result(wait('Automatischer Wiederanlauf',s.charge_retry_s),'waiting');
  const actual=s.actual_a||[],charging=s.feedback_ok&&actual.length===3&&actual.every(Number.isFinite)&&Math.max(...actual)>=1;
  if(!s.enabled)return result(charging?'Stopp angefordert · warte auf Ende der Ladung':'Ladung ausgeschaltet',charging?'waiting':'normal');
  if(s.phase_idle_inhibit)return result('Kein Ladestrom · erneuter Start erforderlich','waiting');
  const pvWait={pv_waiting:'Warte auf PV-Überschuss',pv_starting:'PV-Überschuss vor dem Start prüfen',pv_stopping:'PV-Schwankung überbrücken',pv_cooldown:'Pause vor erneutem PV-Start'};
  if(pvWait[s.block_reason])return result(wait(pvWait[s.block_reason],s.pv_wait_s),'waiting');
  if(!['none','grid_fallback'].includes(s.block_reason))return result(reasons[s.block_reason]||'Ladung pausiert','waiting');
  const phases=`${s.charge_phases}-phasig`;
  let text=charging?`Lädt · ${phases}`:`Start angefordert · ${phases} · warte auf Fahrzeug`;
  if(charging&&s.mode==='manual'&&Number.isFinite(s.estimate_w)&&Number.isFinite(s.target_current_a)&&Math.abs(s.estimate_w/1000-s.target_current_a*powerFactor(s.charge_phases))>.3)text=`Ladeleistung wird angepasst · ${phases}`;
  if(s.battery_buffer_active)text+=` · Wolkenpuffer ${Math.ceil(s.battery_buffer_remaining_s/60)} min`;
  else if(s.battery_assist_active)text+=' · Hausakku unterstützt';
  if(s.phase_wait_kind==='hold')text+=` · Phasenwechsel frühestens in ${s.phase_wait_s} s`;
  if(s.phase_wait_kind==='candidate')text+=' · '+wait('Phasenwechsel wird vorbereitet',s.phase_wait_s);
  if(s.block_reason==='grid_fallback')text+=' · Haus-Phasenwerte fehlen, maximal 8 kW';
  if(s.evcc_test_enabled)text='evcc regelt · '+text;
  return result(text,charging?'normal':'waiting');
}

function chargeButtonLabel(s,online,charging){
  if(s?.firmware_check_pending)return "Firmware wird geprüft";
  if(s?.firmware_check_failed)return "Firmware-Prüfung fehlgeschlagen";
  if(!online)return 'Verbindung fehlt';
  if(s.phase_test_active)return 'Schütztest · Stoppen';
  if(s.evcc_test_enabled&&!s.enabled)return 'evcc steuert';
  if(s.phase_state==='fehler'&&(s.enabled||s.evcc_enabled))return 'Gestört · Stoppen';
  if(s.phase_state==='fehler'&&s.can_unlock===false)return 'Gestoppt · Einrichtung prüfen';
  if(s.charge_stop_latched||s.phase_state==='fehler')return 'Gesperrt · Entsperren';
  if(!s.enabled)return charging?'Stoppt …':'Gestoppt · Starten';
  if(s.phase_switch_enabled&&s.phase_state!=='bereit')return 'Phasenwechsel · Stoppen';
  if(charging)return 'Lädt · Stoppen';
  if(s.mode==='pv')return 'PV wartet · Stoppen';
  return 'Startet · Stoppen';
}

function render(){if(!state)return;const s=state,valid=online;

  $('system-version').textContent=s.version?`V${s.version}`:'—';



  const energy=s.energy[source],actual=s.actual_a||s.meter_a, measured=valid&&s.feedback_ok&&actual.every(Number.isFinite), peak=measured?Math.max(...actual):null, idle=measured&&peak<1, charging=measured&&!idle;

  let message='';

  if(!['none','off','manual_stop','below_minimum','pv_waiting','pv_starting','pv_cooldown','pv_stopping','battery_reserve','evcc_waiting','evcc_local_stop','evcc_status'].includes(s.block_reason))message=reasons[s.block_reason]||'Status unbekannt.';

  if(s.reboot_required)message='Einstellungen gespeichert. Bitte neu starten.';

  if(!s.storage_ok)message='Speicherfehler: Verbrauch kann nicht zuverlässig gesichert werden.';

  if(s.stop_requested&&charging)message='Stop angefordert. Die Wallbox nimmt noch Strom auf.';

  const status=controlStatus(s,valid);
  if(message&&valid){status.text=message;status.kind='fault';}
  $('control-status').hidden=false;
  $('control-status-text').textContent=status.text;
  $('control-status').dataset.kind=status.kind;
  const locked=s.charge_stop_latched||s.phase_state==='fehler';
  $('unlock-control').hidden=!locked;
  $('unlock-control').disabled=!valid||busy||s.can_unlock===false;
  $('unlock-control').title=s.can_unlock===false?'Zuerst Schützstellung und Messwerte prüfen':'Sperre aufheben · Ladung bleibt gestoppt';

  function lamp(id,ok,label){const el=$(id);el.classList.toggle('ok',valid&&ok);el.classList.toggle('fault',!valid||!ok);el.querySelector('small').textContent=valid&&ok?'OK':'Fehlt';el.title=label;}

  lamp('wallbox-lamp',s.wallbox_ok&&s.last_sent_age_ms!=null&&s.last_sent_age_ms<10000,'Aktuelle Stromabfragen der Wallbox beantwortet');lamp('meter-lamp',s.meter_ok,'Aktuelle Daten vom Wallbox-Stromzähler');

  const watts=s.wallbox_w==null?s.estimate_w:s.wallbox_w;
  const previousWallbox=TeeNetHealth.reading(s,'wallbox',valid);
  const staleWallbox=!measured&&previousWallbox.value!==null;
  $('power').dataset.stale=String(staleWallbox);
  $('status-help').hidden=!status.help;
  $('status-help').href=s.basic_mode&&status.help==='#diagnostic-settings'?'#maintenance':status.help||'#diagnostic-settings';

  $('power').textContent=format(staleWallbox?previousWallbox.value/1000:!valid?null:idle?0:watts==null?null:watts/1000);

  $('power-note').textContent=staleWallbox?`Letzter Messwert · ${TeeNetHealth.ageText(previousWallbox.age)}`:idle?'Keine Ladung':!measured?'Messwerte fehlen':s.wallbox_w==null?'Aus Strommessung geschätzt':'Gemessene Wirkleistung';


  $('charging').textContent=!measured?'Messung fehlt':charging?'Lädt':s.charge_stop_latched?'Start gesperrt':s.charge_retry_s>0?'Wiederanlaufpause':s.enabled&&s.target_current_a>0?'Start angefordert':s.mode==='pv'&&s.enabled?'Wartet auf Leistung':'Bereit';$('charging').classList.toggle('muted-pill',!charging);

  $('today').textContent=format(energy.today_kwh,2);$('total').textContent=format(energy.total_kwh,2);$('coverage').textContent=`${format(energy.today_charging_s/3600,1)} h`;
  const resetDate=String(energy.total_reset_date||'');$('total-reset-date').textContent=resetDate.length===8?`seit ${resetDate.slice(6,8)}.${resetDate.slice(4,6)}.${resetDate.slice(0,4)}`:'seit Aufzeichnung';
  const carSoc=Number.isFinite(s.car_soc_pct)?s.car_soc_pct:null;
  $('car-soc-row').hidden=!s.vehicle_soc_enabled||carSoc===null;
  $('car-soc').textContent=format(carSoc,0);
  document.querySelector('.scene-detail').classList.toggle('has-car-soc',carSoc!==null);

  $('time-note').textContent=s.clock_ok?'':'Uhrzeit wird synchronisiert …';$('time-note').hidden=s.clock_ok;

  $('current').max=powerSteps().length-1;
  if(!dirty){const mode=displayedChargeMode(s);$('mode').value=mode;if(s.enabled&&!s.evcc_test_enabled)rememberChargeMode(mode);$('current').value=powerStepForCurrent(s.current_a);}

  document.querySelector('.mode-picker').hidden=!!s.basic_mode;
  if(s.basic_mode||s.zero_feed_enabled===false)$('mode').value='manual';
  $('pv-allocation-status').hidden=!s.pv_allocation_enabled||$('mode').value!=='pv';
  $('pv-allocation-status').textContent=s.pv_allocation_ready?`PV-Priorität: ${['Hausakku zuerst','Auto zuerst','Anteilig aufteilen'][s.pv_priority]}`:'PV-Priorität pausiert · Akkuleistungswerte fehlen';
  document.querySelectorAll('[data-mode]').forEach(el=>el.setAttribute('aria-pressed',String(el.dataset.mode===$('mode').value)));

  $('manual-current-label').hidden=$('mode').value!=='manual';
  updateDesiredPower();

  const lastSoc=TeeNetHealth.reading(s,'battery',valid),soc=valid&&Number.isFinite(s.battery_soc_pct)?Math.max(0,Math.min(100,s.battery_soc_pct)):lastSoc.value;
  $('battery-soc-label').textContent=!lastSoc.fresh&&soc!==null?`% · ${TeeNetHealth.ageText(lastSoc.age)}`:'% Ladezustand';
  $('battery-soc').dataset.stale=String(!lastSoc.fresh&&soc!==null);$('battery-soc').title=!lastSoc.fresh&&soc!==null?`Letzter Ladezustand · ${TeeNetHealth.ageText(lastSoc.age)}`:'Aktueller Ladezustand';
  $('battery-card').hidden=!s.battery_protect;
  $('house-battery-power').hidden=!s.battery_protect;
  document.querySelector('.home-grid').classList.toggle('battery-hidden',!s.battery_protect);
  const batteryInactive=$('mode').value!=='pv';$('battery-card').dataset.inactive=String(batteryInactive);$('battery-mode-note').hidden=true;
  document.querySelectorAll('.battery-choice').forEach(el=>el.hidden=batteryInactive);writes();
  $('battery-soc').textContent=format(soc,0);
  if(!batterySliderDirty)$('battery-reserve-slider').value=String(Math.round(s.battery_reserve_soc/5)*5);
  updateBatterySlider();
  $('battery-fill').style.width=`${soc??0}%`;
  $('battery-toggle').textContent=!s.battery_protect?'Einrichtung nötig':s.battery_use?'Wolkenpuffer an':'Wolkenpuffer aus';
  $('battery-toggle').setAttribute('aria-pressed',String(!!s.battery_use));
  const bufferTime=`${Math.floor(s.battery_buffer_remaining_s/60)}:${String(s.battery_buffer_remaining_s%60).padStart(2,'0')} min`;
  $('battery-buffer-note').textContent=s.battery_buffer_active?`Aktiv · noch ${bufferTime}`:s.battery_use&&s.battery_buffer_expired?'Pufferzeit abgelaufen · warte auf stabilen Solarstrom':s.battery_use&&s.battery_buffer_remaining_s>0?`Verbleibend ${bufferTime}`:`Bis ${format(s.battery_cloud_limit_w/1000,1)} kW · maximal ${Math.ceil((s.battery_buffer_duration_s||900)/60)} Minuten`;
  $('battery-start-toggle').textContent=!s.battery_protect?'Einrichtung nötig':s.battery_start_use?'Akku-Laden an':'Akku-Laden aus';
  $('battery-start-toggle').setAttribute('aria-pressed',String(!!s.battery_start_use));
  $('battery-start-note').textContent=`Bis ${format(s.battery_discharge_limit_w/1000,1)} kW · Hausakku bis ${format(s.battery_reserve_soc,0)} % nutzen`;
  const steps=powerSteps();
  $('target').textContent=$('mode').value==='manual'?(steps.length>1?'':'Keine gültige Ladestufe. Ladegrenzen prüfen.'):s.mode==='pv'&&s.enabled?(reasons[s.block_reason]==reasons.none?(charging?'Überschussladung läuft.':'Warte auf Ladeleistung vom Fahrzeug.'):(reasons[s.block_reason]||'Überschussladung')+(s.pv_wait_s>0?` Noch ${s.pv_wait_s} s.`:'')):'Starten, um PV-Überschuss zu nutzen.';
  $('target').hidden=!$('target').textContent||!!s.charge_plan?.active;

  $('apply-control').textContent=chargeButtonLabel(s,valid,charging);
  $('apply-control').classList.toggle('primary-button',!!s.enabled);
  $('apply-control').setAttribute('aria-pressed',String(!!s.enabled));
  $('charge-target').textContent=format(valid?s.target_current_a*powerFactor():null,1);

  $('session-prices').textContent=`Netz ${format((s.grid_price_kwh??.25)*100,0)} ct · Solar / Akku ${format((s.solar_price_kwh??.08)*100,0)} ct je kWh`;

  const lastHouse=TeeNetHealth.reading(s,'house',valid),house=valid&&s.house_meter_ok&&Number.isFinite(s.house_power_w)?s.house_power_w:lastHouse.value, direction=house==null?'unknown':house>50?'import':house< -50?'export':'balanced';

  $('house-power').textContent=house==null?'—':`${format(Math.abs(house)/1000,2)} kW`;$('house-caption').textContent={unknown:'Hauszählerdaten fehlen',import:'Netzbezug',export:'Einspeisung',balanced:'Ausgeglichen'}[direction];$('house-card').dataset.direction=lastHouse.fresh?direction:'unknown';$('house-power').dataset.stale=String(!lastHouse.fresh&&house!==null);if(!lastHouse.fresh&&house!==null)$('house-caption').textContent=`Letzter Messwert · ${TeeNetHealth.ageText(lastHouse.age)}`;
  const lastPv=TeeNetHealth.reading(s,'pv',valid),pv=valid&&Number.isFinite(s.pv_generation_w)?s.pv_generation_w:lastPv.value;
  $('pv-power').dataset.stale=String(!lastPv.fresh&&pv!==null);$('pv-reading').querySelector('small').textContent=!lastPv.fresh&&pv!==null?`PV-Erzeugung · ${TeeNetHealth.ageText(lastPv.age)}`:'PV-Erzeugung';
  $('pv-power').textContent=`${format(pv===null?null:pv/1000,2)} kW`;
  $('pv-reading').dataset.fresh=String(pv!==null);$('pv-reading').hidden=!s.pv_display_enabled;
  $('house-card').hidden=!(s.zero_feed_enabled||s.grid_guard_enabled||s.pv_display_enabled||s.battery_protect);
  const charge=valid&&Number.isFinite(s.battery_charge_w)?s.battery_charge_w:null,discharge=valid&&Number.isFinite(s.battery_discharge_w)?s.battery_discharge_w:null;
  const batteryFlow=charge===null||discharge===null?'unknown':charge-discharge>50?'charging':discharge-charge>50?'discharging':'idle';
  $('house-battery-power').dataset.direction=batteryFlow;
  $('house-battery-label').textContent={charging:'Akku lädt',discharging:'Akku entlädt',idle:'Akku bereit',unknown:'Akkuleistung'}[batteryFlow];
  $('house-battery-value').textContent=`${format(batteryFlow==='unknown'?null:Math.max(0,Math.abs(charge-discharge))/1000,2)} kW`;
  const mqttSource=s.mqtt_input_source==='opendtu'?'OpenDTU':'Smart Home';
  const signal=Number.isFinite(s.wifi_rssi_dbm)?` · ${s.wifi_rssi_dbm} dBm`:'';
  const diagnostics=[diagnostic('Hardware',s.hardware||'ESP32-S3'),diagnostic('Heimnetz',s.wifi_ok?`${s.station_ip}${signal}`:'Nicht verbunden'),diagnostic('Uhrzeit',s.clock_ok?'Synchronisiert':'Warte auf Internetzeit'),diagnostic('MQTT',s.mqtt_ok?`Verbunden · ${mqttSource}`:`Nicht verbunden · ${mqttSource}`),diagnostic('Wallbox',s.wallbox_ok?'Kommunikation OK':'Keine aktuellen Abfragen'),diagnostic('Wallbox-Zähler',s.feedback_ok?'Daten aktuell':'Daten fehlen'),diagnostic('Hauszähler',s.house_meter_ok?'Daten aktuell':s.zero_feed_enabled||s.grid_guard_enabled?'Daten fehlen':'Nicht benötigt'),diagnostic('Speicherung',s.storage_ok?'Bereit':'Fehler'),...(s.huawei_enabled?[diagnostic('Huawei',s.huawei_ok?s.huawei_model:'Keine aktuellen Daten')]:[])];
  diagnostics.push(diagnostic('Laufzeit seit Reset',window.TeeNetHealth?.uptimeText(s.uptime_s)||'—'));
  if(typeof s.firmware_check_ok==='boolean')diagnostics.push(diagnostic('Firmware-Selbsttest',s.firmware_check_ok?'Bestanden':s.firmware_check_failed?'Fehlgeschlagen · Ladung gesperrt':'Prüfung läuft'));
  if([s.free_heap,s.min_free_heap,s.largest_free_block].every(Number.isFinite))diagnostics.push(diagnostic('Arbeitsspeicher',`${format(s.free_heap/1024,0)} KiB frei · Minimum ${format(s.min_free_heap/1024,0)} KiB · größter Block ${format(s.largest_free_block/1024,0)} KiB`));
  if(s.external_mode_input_enabled)diagnostics.push(diagnostic('Betriebsart-Kontakt',s.external_mode_contact?'PV-Überschuss':'Manuell'));
  if(s.evu_input_enabled)diagnostics.push(diagnostic('Extern Start / Stopp / EVU-Kontakt',s.evu_active?'Aktiv':'Frei'));
  if(s.huawei_enabled&&s.house_meter_type==='huawei')diagnostics.push(diagnostic('Hausströme',s.house_current_ok&&Array.isArray(s.house_a)?s.house_a.map((a,i)=>`L${i+1} ${format(a,2)} A`).join(' · '):'Keine gültige dreiphasige Messung'));
  if(s.grid_guard_enabled)diagnostics.push(diagnostic('Hausanschlussschutz',s.grid_guard_ok?'Messung OK':'Phasenwerte fehlen · maximal 8 kW'));
  $('diagnostics').replaceChildren(...diagnostics);

  window.teennetFeatures?.render();
  if(typeof window!=='undefined' && window.dispatchEvent && typeof CustomEvent==='function')window.dispatchEvent(new CustomEvent('teenet-status',{detail:s}));

}

function chartColors(){return document.documentElement.dataset.theme==='dark'?{text:'#a4b7c6',grid:'#304454',line:'#54c5e3',fill:'#54c5e31c'}:{text:'#728378',grid:'#edf1ed',line:'#438b70',fill:'#2c806717'};}
function chartBase(canvas,max,unit){const rect=canvas.getBoundingClientRect(),ratio=window.devicePixelRatio||1;canvas.width=Math.round(rect.width*ratio);canvas.height=Math.round(rect.height*ratio);const c=canvas.getContext('2d'),colors=chartColors();c.scale(ratio,ratio);const w=rect.width,h=rect.height,left=44,right=12,top=17,bottom=30;const plot={x:left,y:top,w:w-left-right,h:h-top-bottom};c.font='10px system-ui';c.fillStyle=colors.text;c.fillText(unit,3,10);for(let i=0;i<=4;i++){const y=top+plot.h*i/4;c.strokeStyle=colors.grid;c.beginPath();c.moveTo(left,y);c.lineTo(w-right,y);c.stroke();c.fillStyle=colors.text;c.fillText(format(max*(1-i/4),max>=10?0:1),3,y+4);}return {c,plot,w,h};}

function drawCharts(){if(!$('statistics').hidden&&!document.hidden)drawPower();}

function drawPower(){const key=source===0?'estimate_w':'wallbox_w',points=history.points||[],values=points.filter(p=>p[key]!==null).map(p=>p[key]/1000);const max=Math.max(1,...values)*1.15,{c,plot}=chartBase($('power-chart'),max,'kW');$('power-empty').hidden=values.length>0;let previous=null;

  const colors=chartColors();points.forEach(p=>{const value=p[key];if(value===null){previous=null;return;}const x=plot.x+plot.w*(1-p.age_min/119),y=plot.y+plot.h*(1-value/1000/max);if(previous&&previous.age-p.age_min===1){c.fillStyle=colors.fill;c.beginPath();c.moveTo(previous.x,plot.y+plot.h);c.lineTo(previous.x,previous.y);c.lineTo(x,y);c.lineTo(x,plot.y+plot.h);c.closePath();c.fill();c.strokeStyle=colors.line;c.lineWidth=2;c.beginPath();c.moveTo(previous.x,previous.y);c.lineTo(x,y);c.stroke();}c.fillStyle=colors.line;c.beginPath();c.arc(x,y,2.3,0,Math.PI*2);c.fill();previous={x,y,age:p.age_min};});

  c.fillStyle=colors.text;c.textAlign='left';c.fillText('−2 h',plot.x,plot.y+plot.h+22);c.textAlign='center';c.fillText('−1 h',plot.x+plot.w/2,plot.y+plot.h+22);c.textAlign='right';c.fillText('Jetzt',plot.x+plot.w,plot.y+plot.h+22);$('power-chart').title=values.length?`${values.length} Minuten mit Daten. Höchstes Minutenmittel: ${format(Math.max(...values))} kW.`:'Keine Messdaten';}

let sessionOffset=0,sessionCount=0,sessionsBusy=false;
function selectedPeriod(){return $('export-range').value==='year'?$('export-year').value:$('export-month').value;}
function sessionQuery(){return $('export-range').value==='custom'?`from=${encodeURIComponent($('export-from').value)}&to=${encodeURIComponent($('export-to').value)}`:`period=${encodeURIComponent(selectedPeriod())}`;}
function sessionShare(value,total,cost){return `${format(value,2)} kWh · ${format(cost,2)} € · ${format(total?value/total*100:0,0)} %`;}
function renderSessions(data){
  const total=data.grid_kwh+data.solar_kwh+data.unknown_kwh;
  sessionCount=data.count;
  $('session-total').textContent=`${format(total,2)} kWh`;
  $('session-grid').textContent=sessionShare(data.grid_kwh,total,data.grid_cost_eur);
  $('session-solar').textContent=sessionShare(data.solar_kwh,total,data.solar_cost_eur);
  $('session-cost').textContent=`${format(data.cost_eur,2)} €`;
  const rows=data.days.map(s=>{
    const row=document.createElement('tr');
    function cell(main,detail){const td=document.createElement('td');td.dataset.label=['Tag','Energie','Netz','Solar / Akku','Kosten'][row.children.length];const content=document.createElement('span');content.textContent=main;if(detail){const small=document.createElement('small');small.textContent=detail;content.append(small);}td.append(content);row.append(td);}
    const day=String(s.date),dayLabel=day.length===8?`${day.slice(6,8)}.${day.slice(4,6)}.${day.slice(0,4)}`:'Datum unbekannt';
    cell(dayLabel,`${s.sessions} ${s.sessions===1?'Ladesitzung':'Ladesitzungen'} · ${format(s.charging_s/60,0)} Min. Ladezeit${s.active?' · Läuft / kurze Pause':''}`);
    cell(`${format(s.energy_kwh,2)} kWh`);
    cell(`${format(s.grid_kwh,2)} kWh · ${format(s.grid_cost_eur,2)} €`,`${format(s.energy_kwh?s.grid_kwh/s.energy_kwh*100:0,0)} %`);
    cell(`${format(s.solar_kwh,2)} kWh · ${format(s.solar_cost_eur,2)} €`,`${format(s.energy_kwh?s.solar_kwh/s.energy_kwh*100:0,0)} %`);
    cell(`${format(s.cost_eur,2)} €`);
    return row;
  });
  $('session-rows').replaceChildren(...rows);$('session-empty').hidden=data.days.length>0;
  $('session-empty').textContent='Noch keine Ladesitzungen in diesem Zeitraum.';
  $('session-page').textContent=data.count?`${sessionOffset+1}–${Math.min(sessionOffset+20,data.count)} von ${data.count} Tagen`:'0 Tage';
  $('sessions-prev').disabled=sessionOffset===0;$('sessions-next').disabled=sessionOffset+20>=data.count;
}
function clearSessions(message){
  for(const id of ['session-total','session-grid','session-solar','session-cost'])$(id).textContent='—';
  sessionCount=0;$('session-rows').replaceChildren();$('session-empty').hidden=false;$('session-empty').textContent=message;
  $('session-page').textContent='';$('sessions-prev').disabled=$('sessions-next').disabled=true;
}
function validSessionPeriod(){
  if($('export-range').value==='custom')return /^\d{4}-\d{2}-\d{2}$/.test($('export-from').value)&&/^\d{4}-\d{2}-\d{2}$/.test($('export-to').value)&&$('export-from').value<=$('export-to').value;
  return /^\d{4}(-\d{2})?$/.test(selectedPeriod());
}
async function loadSessions(){
  if(sessionsBusy||!online||document.hidden)return;
  const query=sessionQuery(),offset=sessionOffset;
  if(!validSessionPeriod()){clearSessions('Bitte einen gültigen Zeitraum wählen: Von darf nicht nach Bis liegen.');return;}
  lastSessionsAt=Date.now();
  sessionsBusy=true;
  try{const data=await api(`/api/sessions?${query}&offset=${offset}`);if(query===sessionQuery()&&offset===sessionOffset)renderSessions(data);}
  catch(error){if(query===sessionQuery()&&offset===sessionOffset)clearSessions('Sitzungsdaten gerade nicht erreichbar.');}
  finally{sessionsBusy=false;if(query!==sessionQuery()||offset!==sessionOffset)loadSessions();}
}
function changeSessionPeriod(){
  const range=$('export-range').value;$('export-month-label').hidden=range!=='month';$('export-year-label').hidden=range!=='year';$('export-from-label').hidden=$('export-to-label').hidden=range!=='custom';
  sessionOffset=0;const valid=validSessionPeriod();$('session-csv').setAttribute('aria-disabled',String(!valid));$('session-csv').href=`/api/export.csv?${sessionQuery()}`;if(!valid)clearSessions('Bitte einen gültigen Zeitraum wählen: Von darf nicht nach Bis liegen.');else loadSessions();
}
function localDateValue(date){return `${date.getFullYear()}-${String(date.getMonth()+1).padStart(2,'0')}-${String(date.getDate()).padStart(2,'0')}`;}
const initialDate=new Date(),initialYear=initialDate.getFullYear();
$('export-month').value=localDateValue(initialDate).slice(0,7);$('export-year').value=initialYear;$('export-from').value=`${initialYear}-01-01`;$('export-to').value=localDateValue(initialDate);
$('session-csv').addEventListener('click',e=>{if(!validSessionPeriod()){e.preventDefault();toast('Bitte einen gültigen Zeitraum wählen.',true);}});
for(const id of ['export-range','export-month','export-year','export-from','export-to'])$(id).addEventListener('change',changeSessionPeriod);
$('sessions-prev').addEventListener('click',()=>{sessionOffset=Math.max(0,sessionOffset-20);loadSessions();});
$('sessions-next').addEventListener('click',()=>{if(sessionOffset+20<sessionCount){sessionOffset+=20;loadSessions();}});
$('session-csv').href=`/api/export.csv?${sessionQuery()}`;

function shellPinChoices(values,field){
  const occupied=new Map();
  const activeState=typeof state==='undefined'?null:state;
  if(Number(values.meter_rs485_interface||0)===0&&!['shelly_gen2','shelly_em1','em24_tcp'].includes(values.wallbox_meter_type)){
    occupied.set(Number(values.xemex_tx_pin??activeState?.gpio_pins?.xemex_tx_pin??activeState?.default_meter_tx_pin??4),'Wallbox-Zähler TX');
    occupied.set(Number(values.xemex_rx_pin??activeState?.gpio_pins?.xemex_rx_pin??activeState?.default_meter_rx_pin??5),'Wallbox-Zähler RX');
  }
  if(Number(values.house_rs485_interface||0)===0&&['xemex','sdm630','sdm630mct'].includes(values.house_meter_type)){occupied.set(Number(values.house_tx_pin??activeState?.gpio_pins?.house_tx_pin??43),'Hauszähler TX');occupied.set(Number(values.house_rx_pin??activeState?.gpio_pins?.house_rx_pin??44),'Hauszähler RX');}
  if(values.external_mode_input_enabled)occupied.set(Number(values.external_mode_input_pin??activeState?.gpio_pins?.external_mode_input_pin??activeState?.external_mode_input_pin??6),'Betriebsart-Kontakt');
  if(values.evu_input_enabled)occupied.set(Number(values.evu_input_pin??activeState?.gpio_pins?.evu_input_pin??7),'Extern Start / Stopp / EVU-Kontakt');
  if(values.relay_board_enabled&&Number(values.phase_actuator_type||0)===0){occupied.set(Number(values.relay1_pin??activeState?.gpio_pins?.relay1_pin??12),'Phasenschütz');}
  if(values.phase_switch_enabled&&values.phase_feedback_enabled!==false&&Number(values.phase_actuator_type||0)===0)occupied.set(Number(values.phase_feedback_pin??activeState?.gpio_pins?.phase_feedback_pin??13),'Schütz-Rückmeldung');
  if(values.phase_switch_enabled&&Number(values.phase_feedback_count)===2&&Number(values.phase_actuator_type||0)===0)occupied.set(Number(values.phase_feedback_second_pin??activeState?.gpio_pins?.phase_feedback_second_pin??14),'Schütz-Rückmeldung 2');
  const other=field==='wallbox_tx_pin'?'wallbox_rx_pin':'wallbox_tx_pin';
  occupied.set(Number(values[other]),field==='wallbox_tx_pin'?'Shell RX':'Shell TX');
  const led=Number(values.status_led_pin??activeState?.gpio_pins?.status_led_pin??48);if(led>=0)occupied.set(led,'RGB-LED');
  return (activeState?.hardware_id==='andreas-testboard'?[1,2,5,6,7,8,38,39]:[1,2,4,5,6,7,8,9,10,11,12,13,14,15,16,17,18,21,38,39,40,41,42,43,44,47]).map(pin=>({pin,reason:occupied.get(pin)||''}));
}
function updateShellPins(){
  const f=$('config-form').elements,values={house_meter_type:f.house_meter_type.value,wallbox_meter_type:f.wallbox_meter_type.value};
  for(const key of ['external_mode_input_enabled','evu_input_enabled','relay_board_enabled','phase_switch_enabled','phase_feedback_enabled'])values[key]=f[key].checked;
  Object.assign(values,configBaseline);
  for(const key of ['external_mode_input_enabled','evu_input_enabled','relay_board_enabled','phase_switch_enabled','phase_feedback_enabled'])values[key]=f[key].checked;
  values.house_meter_type=f.house_meter_type.value;values.wallbox_meter_type=f.wallbox_meter_type.value;
  values.phase_actuator_type=Number(f.phase_actuator_type?.value||0);
  values.phase_feedback_count=Number(f.phase_feedback_count?.value||1);
  values.meter_rs485_interface=f.meter_rs485_interface.value;values.house_rs485_interface=f.house_rs485_interface.value;
  const defaultTx=Number(state?.default_shell_tx_pin||17),defaultRx=Number(state?.default_shell_rx_pin||18);
  values.wallbox_tx_pin=Number(f.wallbox_tx_pin.value||defaultTx);values.wallbox_rx_pin=Number(f.wallbox_rx_pin.value||defaultRx);
  let conflict=false;
  for(const [name,standard] of [['wallbox_tx_pin',defaultTx],['wallbox_rx_pin',defaultRx]]){
    const select=f[name],selected=values[name],choices=shellPinChoices(values,name),chosen=choices.find(c=>c.pin===selected);
    if(!chosen)choices.push({pin:selected,reason:'Nicht geeignet'});
    select.replaceChildren(...choices.map(({pin,reason})=>{const option=document.createElement('option');option.value=String(pin);option.textContent=`GPIO ${pin}${reason?' · belegt: '+reason:pin===standard?' · Standard':''}`;option.disabled=!!reason;return option;}));
    select.value=String(selected);const invalid=Number(f.shell_rs485_interface.value)===0&&(!chosen||!!chosen.reason);conflict||=invalid;
    select.setCustomValidity(invalid?'Dieser Shell-Pin ist belegt. Bitte einen freien Pin wählen.':'');select.setAttribute('aria-invalid',String(invalid));select.classList.toggle('modified-setting',selected!==standard);
  }
  $('shell-pin-warning').hidden=!conflict;$('shell-pin-warning').textContent=conflict?'Pin bereits belegt. Freien Pin wählen.':'';
  $('shell-pin-default-note').textContent=`Standard für ${state?.hardware||'dieses Board'}: TX ${defaultTx} / RX ${defaultRx}. Nur freie Pins wählen; danach neu starten.`;
  if($('evu-pin-note'))$('evu-pin-note').textContent=`GPIO${Number(configBaseline.evu_input_pin??state?.gpio_pins?.evu_input_pin??7)} gegen GND = aktiv. 0 A: geschlossen stoppt, offen hebt die Kontaktsperre auf. Höhere Werte begrenzen ab Mindestladestrom. Nur potentialfreie Kontakte.`;
  $('mode-contact-note').textContent=`GPIO${Number(configBaseline.external_mode_input_pin??state?.gpio_pins?.external_mode_input_pin??state?.external_mode_input_pin??6)}: offen = Manuell · gegen GND = PV-Überschuss.`;
}
$('shell-pins-default').addEventListener('click',()=>{const f=$('config-form').elements,tx=Number(state?.default_shell_tx_pin||17),rx=Number(state?.default_shell_rx_pin||18);f.wallbox_tx_pin.value=String(tx);f.wallbox_rx_pin.value=String(rx);updateShellPins();toast(`TX ${tx} / RX ${rx} ausgewählt. Zum Übernehmen speichern.`);});

const additionalFeatureFields=['evcc_feature_enabled','pv_allocation_enabled','zero_feed_enabled','charge_plan_enabled','battery_protect','vehicle_soc_enabled','pv_display_enabled','relay_board_enabled','external_mode_input_enabled','evu_input_enabled','grid_guard_enabled','phase_switch_enabled','huawei_enabled','huawei_battery','huawei_pv'];
function normalizeBasicFields(){
 const f=$('config-form').elements;if(f.basic_mode.value!=='true')return;
 for(const name of additionalFeatureFields)f[name].checked=false;
 f.mqtt_input_source.value='0';$('opendtu-enabled').checked=false;
 f.house_meter_type.value='tasmota';
}
function updateFunctionMode(){
 const basic=$('config-form').elements.basic_mode.value==='true';
 document.documentElement.dataset.basic=String(basic);
 $('additional-functions').hidden=basic;
 document.querySelectorAll('[data-function-mode]').forEach(button=>button.setAttribute('aria-pressed',String((button.dataset.functionMode==='basic')===basic)));
 $('function-mode-note').textContent=basic?'':'Nur benötigte Funktionen auswählen.';
 $('function-mode-note').hidden=basic;
}
document.querySelectorAll('[data-function-mode]').forEach(button=>button.addEventListener('click',()=>{
 const f=$('config-form').elements;f.basic_mode.value=String(button.dataset.functionMode==='basic');
 normalizeBasicFields();updateExpertMode();$('equipment-settings').open=true;
 f.basic_mode.dispatchEvent(new Event('change',{bubbles:true}));
 writes();
}));

function updateEquipment(){const f=$('config-form').elements,checked=name=>f[name].checked,basic=f.basic_mode.value==='true',expert=!basic;
 updateFunctionMode();
 $('evcc-settings').hidden=basic||!checked('evcc_feature_enabled');
 const meterSerial=!['shelly_gen2','shelly_em1','em24_tcp'].includes(f.wallbox_meter_type.value),houseSerial=['xemex','sdm630','sdm630mct'].includes(f.house_meter_type.value);
 for(const role of ['shell','meter','house']){
   const tcp=Number(f[role+'_rs485_interface'].value)===1;
   $(role+'-gateway-field').hidden=!tcp;$(role+'-gateway-note').hidden=!tcp;
   f[role+'_rs485_host'].required=tcp&&(role==='shell'||(role==='meter'?meterSerial:houseSerial));
 }
 $('meter-transport-settings').hidden=!meterSerial;$('house-transport-settings').hidden=!houseSerial;
 $('shell-uart-pins').hidden=Number(f.shell_rs485_interface.value)===1;

 $('shell-address').hidden=!checked('shell_limits_auto');$('shell-manual-limits').hidden=checked('shell_limits_auto')&&!!state?.shell_limits_ok;
 $('relay-settings').hidden=basic||!checked('relay_board_enabled');
 $('hardware-settings').hidden=basic||(!checked('external_mode_input_enabled')&&!checked('evu_input_enabled'));
 $('mode-contact-settings').hidden=!checked('external_mode_input_enabled');$('evu-contact-settings').hidden=!checked('evu_input_enabled');
 $('mqtt-settings').hidden=false;$('mqtt-connection-details').hidden=!checked('mqtt_enabled');$('vehicle-settings').hidden=basic||!checked('vehicle_soc_enabled');
 $('battery-settings').hidden=basic||!checked('battery_protect');$('phase-settings').hidden=!checked('phase_switch_enabled');$('fixed-phase-setting').hidden=checked('phase_switch_enabled');if(checked('phase_switch_enabled'))f.fixed_charge_phases.value='3';
 f.phase_switch_enabled.closest('label').hidden=true;

 $('house-settings').hidden=basic||!(checked('zero_feed_enabled')||checked('grid_guard_enabled'));$('maintenance').hidden=false;$('diagnostic-settings').hidden=false;

 $('huawei-settings').hidden=basic||!checked('huawei_enabled');
 const opendtu=Number(f.mqtt_input_source.value)===2;
 $('opendtu-enabled').checked=opendtu;$('opendtu-settings').hidden=basic||!opendtu;
 $('opendtu-pv-settings').hidden=!checked('pv_display_enabled');
 $('opendtu-current-field').hidden=!checked('battery_protect');
 const huaweiOverrides=checked('huawei_enabled')&&((checked('huawei_battery')&&checked('battery_protect'))||(checked('huawei_pv')&&checked('pv_display_enabled')));
 $('opendtu-source-note').hidden=!huaweiOverrides;
 $('opendtu-source-note').textContent='Für die in der Huawei-Kachel aktivierten Messwerte hat Huawei Vorrang vor OpenDTU.';
 $('pv-control-settings').hidden=!checked('zero_feed_enabled');
 window.teenetShellyPhase?.update();
 if(f.house_meter_type.value==='huawei'){$('house-host-field').hidden=true;}
 const network=['tasmota','shelly_gen2','shelly_em1','em24_tcp'].includes(f.house_meter_type.value);
 const networkWallbox=f.wallbox_meter_type.value.startsWith('shelly_')||f.wallbox_meter_type.value==='em24_tcp';
 const threePhaseHouse=['xemex','sdm630','sdm630mct','shelly_gen2','huawei','em24_tcp'].includes(f.house_meter_type.value);
 $('house-host-field').hidden=!network;$('house-http-settings').hidden=!expert||f.house_meter_type.value!=='tasmota';
 $('house-shelly-modbus-note').hidden=!f.house_meter_type.value.startsWith('shelly_');
 $('wallbox-network-settings').hidden=!networkWallbox;
 $('meter-bus-settings').hidden=networkWallbox;
 $('wallbox-unit-field').hidden=networkWallbox?f.wallbox_meter_type.value!=='em24_tcp':!expert;
 const warnings=[];
 if(!threePhaseHouse)f.grid_guard_enabled.checked=false;
 if(checked('zero_feed_enabled')&&f.house_meter_type.value==='xemex')warnings.push('Dieser Hauszähler liefert nur Ströme. Für PV eine Quelle mit gerichteter Leistung wählen.');
 $('equipment-warning').textContent=warnings.join(' ');$('equipment-warning').hidden=!warnings.length;
 if(!checked('battery_protect')||!checked('zero_feed_enabled'))f.pv_allocation_enabled.checked=false;
 updatePvAllocation();
 updateModbus();updateHouseQuery();updateWallboxQuery();updateShellPins();updateUnsavedFunctions();

}

function updatePvAllocation(){
 const f=$('config-form').elements,enabled=f.pv_allocation_enabled.checked,priority=Number(f.pv_priority.value);
 $('pv-allocation-settings').hidden=!enabled;$('pv-allocation-default').hidden=enabled;
 $('pv-house-priority-field').hidden=priority!==0;$('pv-car-priority-field').hidden=priority!==1;
 $('pv-share-settings').hidden=priority!==2;
 const house=Number(f.pv_house_priority_w.value),car=Number(f.pv_car_priority_w.value),total=house+car;
 const percent=total>0?house/total*100:40;
 if(document.activeElement!==$('pv-house-share'))$('pv-house-share').value=String(Math.min(98,Math.round(percent)));
 $('pv-house-share-value').textContent=`Hausakku ${format(percent,0)} %`;
 $('pv-car-share-value').textContent=`Auto ${format(100-percent,0)} %`;
 $('pv-house-share').setAttribute('aria-valuetext',`Hausakku ${format(percent,0)} Prozent, Auto ${format(100-percent,0)} Prozent`);
 $('pv-allocation-note').textContent=priority===0?`Bis zu ${format(house,1)} kW bleiben für den Hausakku frei. Mit dem übrigen Solarstrom lädt das Auto.`:priority===1?`Das Auto lädt mit bis zu ${format(car,1)} kW. Der übrige Solarstrom bleibt für den Hausakku.`:`Gewünschte Aufteilung bei 5 kW Solarstrom: ${format(5*percent/100,1)} kW für den Hausakku und ${format(5*(100-percent)/100,1)} kW fürs Auto.`;
}
$('pv-house-share').addEventListener('input',()=>{
 const f=$('config-form').elements,percent=Number($('pv-house-share').value),carFraction=(100-percent)/100;
 // Keep the existing API's power weights; only their ratio matters in split mode.
 const total=10000*Math.max(1,Math.ceil(5/(100-percent)));
 f.pv_house_priority_w.value=String(Number((total*percent/100/1000).toFixed(4)));
 f.pv_car_priority_w.value=String(Number((total*carFraction/1000).toFixed(4)));
 updatePvAllocation();
});
for(const name of ['pv_house_priority_w','pv_car_priority_w'])$('config-form').elements[name].addEventListener('input',updatePvAllocation);

function updateUnsavedFunctions(){
 const f=$('config-form').elements;
 const changed=Array.from(f).some(el=>{
  if(!el.name||el.type==='submit'||!(el.name in configBaseline))return false;
  if(el.hasAttribute('data-secret'))return !!el.value;
  const value=el.type==='checkbox'?el.checked:el.hasAttribute('data-boolean')?el.value==='true':el.type==='number'||el.hasAttribute('data-number')?Number(el.value)*(Number(el.dataset.scale)||1):el.value;
  const baseline=configBaseline[el.name];
  return typeof value==='number'&&typeof baseline==='number'?Math.abs(value-baseline)>.0001:value!==baseline;
 })||Array.from(f).some(el=>el.hasAttribute('data-secret')&&!!el.value)||$('clear-wifi-password').checked||((Number(f.phase_actuator_type.value)===1)&&($('phase-shelly-auth').value==='1')!==!!configBaseline.phase_shelly_password_set)||$('opendtu-enabled').checked!==(Number(configBaseline.mqtt_input_source)===2);
 const saveError=changed&&typeof configSaveError==='string'?configSaveError:'';
  $('functions-unsaved').hidden=!initialized||(!changed&&(!settingsRebootPending||settingsRebootDeferred));
 $('settings-notice-title').textContent=changed?(saveError?'Nicht gespeichert':'Ungespeicherte Änderungen'):'Gespeichert – Neustart erforderlich';
 $('settings-notice-icon').textContent=changed?'✎':'↻';
 $('functions-unsaved').dataset.kind=changed?'unsaved':'restart';
 $('functions-unsaved').dataset.error=saveError?'true':'false';
 $('settings-notice-text').textContent=saveError||(changed?'Bitte Einstellungen speichern.':'Die Änderung ist gespeichert und wird nach dem Neustart aktiv.');
 $('settings-notice-save').hidden=!changed;
 $('settings-notice-restart-actions').hidden=changed||!settingsRebootPending;
 if(typeof window!=='undefined'){window.teenetPhaseTest?.update();window.teenetExpert?.updateWrites();}

}
function updateSaveStatus(result){
 const wasPending=settingsRebootPending;
 const newlyRequired=typeof result.restart_needed==='boolean'?result.restart_needed:
   typeof result.applied==='boolean'?!result.applied:!!result.reboot_required&&!wasPending;
 settingsRebootPending=!!result.reboot_required;
 // A live save must not reopen a restart the user has already postponed.
 if(!settingsRebootPending||newlyRequired)settingsRebootDeferred=false;
 return newlyRequired;
}
$('config-form').addEventListener('input',()=>{configSaveError='';},true);
$('config-form').addEventListener('change',()=>{configSaveError='';},true);
$('config-form').addEventListener('input',updateUnsavedFunctions);
$('config-form').addEventListener('change',e=>{
 const f=$('config-form').elements,name=e.target.name;
 if(e.target.id==='opendtu-enabled'){f.mqtt_input_source.value=e.target.checked?'2':'0';if(e.target.checked)f.mqtt_enabled.checked=true;}
 if(name==='phase_switch_enabled'&&f.phase_switch_enabled.checked){f.relay_board_enabled.checked=true;f.relay1_mode.value='3';}
 if(name==='relay_board_enabled'){f.phase_switch_enabled.checked=f.relay_board_enabled.checked;f.relay1_mode.value=f.relay_board_enabled.checked?'3':'0';}
 if(name==='mqtt_enabled'&&!f.mqtt_enabled.checked){f.mqtt_input_source.value='0';f.vehicle_soc_enabled.checked=false;if(!(f.huawei_enabled.checked&&f.huawei_battery.checked))f.battery_protect.checked=false;if(!(f.huawei_enabled.checked&&f.huawei_pv.checked))f.pv_display_enabled.checked=false;}
 if(name==='house_meter_type'&&f.house_meter_type.value==='huawei')f.huawei_enabled.checked=true;
 if(name==='huawei_enabled'&&!f.huawei_enabled.checked){f.huawei_battery.checked=false;f.huawei_pv.checked=false;if(f.house_meter_type.value==='huawei'){f.house_meter_type.value='tasmota';f.house_meter_host.value='';f.zero_feed_enabled.checked=false;}if(!f.mqtt_enabled.checked){f.battery_protect.checked=false;f.pv_display_enabled.checked=false;}}
 if(name==='huawei_battery'&&f.huawei_battery.checked){f.huawei_enabled.checked=true;f.battery_protect.checked=true;f.zero_feed_enabled.checked=true;f.zero_reserve_w.value='0';}
 if(name==='huawei_pv'&&f.huawei_pv.checked){f.huawei_enabled.checked=true;f.pv_display_enabled.checked=true;}
 if(name==='battery_protect'&&f.battery_protect.checked){if(!(f.huawei_enabled.checked&&f.huawei_battery.checked))f.mqtt_enabled.checked=true;f.zero_feed_enabled.checked=true;if(Number(f.zero_reserve_w.value)>0)f.zero_reserve_w.value='0';}
 if(name==='grid_guard_enabled'&&f.grid_guard_enabled.checked){
  const supported=['xemex','sdm630','sdm630mct','shelly_gen2','huawei','em24_tcp'].includes(f.house_meter_type.value);
  if(!supported){f.grid_guard_enabled.checked=false;toast('Hausanschlussschutz benötigt drei gemessene Phasenströme.',true);}
 }
 if(name==='vehicle_soc_enabled'&&f[name].checked)f.mqtt_enabled.checked=true;
 if(name==='pv_display_enabled'&&f[name].checked&&!(f.huawei_enabled.checked&&f.huawei_pv.checked))f.mqtt_enabled.checked=true;
 updateEquipment();
});
async function loadConfig(){const cfg=await api('/api/config');if(Number(cfg.mqtt_input_source)===1)cfg.mqtt_input_source=0;if(typeof cfg.phase_switch_enabled==='boolean')cfg.relay_board_enabled=cfg.phase_switch_enabled;configBaseline=cfg;for(const [name,value] of Object.entries(cfg)){const el=$('config-form').elements.namedItem(name);if(!el)continue;if(el.type==='checkbox')el.checked=value;else if(el.hasAttribute('data-boolean'))el.value=String(!!value);else if(el.type==='number'){const scale=Number(el.dataset.scale)||1;el.value=Number((Number(value)/scale).toFixed(4));}else if(!el.hasAttribute('data-secret')){if(el.tagName==='SELECT'&&!Array.from(el.options).some(option=>option.value===String(value))){const option=document.createElement('option');option.value=String(value);option.textContent=String(value);el.append(option);}el.value=value;}}batterySliderDirty=false;$('battery-reserve-slider').value=String(Math.round(cfg.battery_reserve_soc/5)*5);updateBatterySlider();updateMeterFields();updateExpertMode();updateEquipment();secretPlaceholders();}
function updateBatterySlider(){$('battery-reserve-view').textContent=`${format(Number($('battery-reserve-slider').value),0)} %`;}
function powerFactor(phases=state?.charge_phases??3){const single=state?.single_power_per_amp_kw;return Number.isFinite(single)?single*phases:.23*phases;}
function phaseForPower(kw){return state?.phase_switch_enabled?(kw>0&&kw<=3.5?1:3):(state?.fixed_charge_phases??3);}
function powerSteps(){
  const factor=powerFactor(3),minimum=(state?.min_charge_a??8.7)*factor,maximum=(state?.max_charge_a??16)*factor;
  // Repeated vehicle tests established 6.0 kW as the stable three-phase floor.
  // Permit only display rounding, never lower the commissioned current floor.
  const three=Array.from({length:11},(_,i)=>6+i*.5).filter(kw=>kw>=minimum-.05&&kw<=maximum+1e-6);
  const one=(state?.phase_switch_enabled||state?.fixed_charge_phases===1)?Array.from({length:5},(_,i)=>1.5+i*.5).filter(kw=>kw>=(state?.min_charge_a??8.7)*powerFactor(1)-.05&&kw<=state.max_charge_a*powerFactor(1)+.05):[];
  return [0,...one,...(state?.fixed_charge_phases===1?[]:three)];
}
function selectedPower(){return powerSteps()[Number($('current').value)]??0;}
function powerStepForCurrent(amps){
  const phases=state?.phase_switch_enabled?state.manual_phases:(state?.fixed_charge_phases??3);
  if(!Number.isFinite(amps)||amps<(state?.min_charge_a??8.7))return 0;
  const steps=powerSteps(),kw=amps*powerFactor(phases);let closest=0;
  for(let i=1;i<steps.length;i++)if(phaseForPower(steps[i])===phases&&(!closest||Math.abs(steps[i]-kw)<Math.abs(steps[closest]-kw)))closest=i;
  return closest;
}
function selectedCurrent(){const kw=selectedPower(),phases=phaseForPower(kw),minimum=state.min_charge_a;return kw===0?0:Math.min(state.max_charge_a,Math.max(minimum,kw/powerFactor(phases)));}
function updateDesiredPower(){const kw=selectedPower(),steps=powerSteps(),phases=phaseForPower(kw);$('desired-power').textContent=kw===0?'Aus':`${format(kw,1)} kW`;$('current').setAttribute('aria-valuetext',kw===0?'Aus':`${format(kw,1)} Kilowatt`);$('power-max').textContent=`${format(steps[steps.length-1],1)} kW`;$('phase-choice').hidden=!state?.phase_switch_enabled||kw===0;$('phase-choice').textContent=`${phases}-phasig`;}

async function action(work){if(busy||!online)return;busy=true;writes();try{await work();}catch(error){toast(error.name==='AbortError'?'Zeitüberschreitung. Verbindung prüfen.':error.message,true);}finally{busy=false;writes();if(powerQueued)queueMicrotask(applyPowerOnRelease);else if(reserveQueued)queueMicrotask(applyReserveOnRelease);}}



function updateMeterFields(){const f=$('config-form').elements,type=f.wallbox_meter_type.value,house=f.house_meter_type.value;$('coil-settings').hidden=type!=='xemex';$('meter-profile-note').textContent=type==='xemex'?'Xemex erprobt · Standard 9600 Baud / 8E1.':type==='sdm630'||type==='sdm630mct'?'Protokoll geprüft; Hardwaretest offen.':type==='sdm230'||type==='sdm120'?'Einphasige Messung mit Hochrechnung. Protokoll geprüft; Hardwaretest offen.':type==='em24_tcp'?'Drei Phasen per Modbus TCP. Protokoll geprüft; Hardwaretest offen.':type==='shelly_gen2'?'Drei Phasen per Modbus TCP. Modbus im Shelly aktivieren.':type==='shelly_em1'?'Eine Phase per Modbus TCP; Hochrechnung auf aktive Ladephasen.':'Zählerprofil auswählen.';const serialHouse=['xemex','sdm630','sdm630mct'].includes(house);$('house-bus-settings').hidden=!serialHouse;$('house-unit-field').hidden=!serialHouse&&house!=='em24_tcp';}
function updateExpertMode(){const f=$('config-form').elements,expert=f.basic_mode.value!=='true';document.documentElement.dataset.expert=String(expert);document.querySelectorAll('[data-expert-option]').forEach(option=>{option.hidden=!expert&&option.value!==option.parentElement.value;option.disabled=!expert&&option.value!==option.parentElement.value;});updateEquipment();}

$('config-form').elements.wallbox_meter_type.addEventListener('change',()=>{const f=$('config-form').elements;f.meter_baud.value='9600';f.meter_format.value=f.wallbox_meter_type.value==='xemex'?'1':'0';updateMeterFields();updateEquipment();});
$('config-form').elements.house_meter_type.addEventListener('change',()=>{const f=$('config-form').elements;f.house_power_path.value='';f.house_meter_baud.value='9600';f.house_meter_format.value=f.house_meter_type.value==='xemex'?'1':'0';f.house_address.value='1';updateMeterFields();updateEquipment();});


function modbusDefaults(bus){const f=$('config-form').elements;return bus==='meter'?{xemex_address:1,meter_baud:9600,meter_format:f.wallbox_meter_type.value==='xemex'?1:0}:{house_address:1,house_meter_baud:9600,house_meter_format:f.house_meter_type.value==='xemex'?1:0};}
function updateModbus(){const f=$('config-form').elements;for(const bus of ['meter','house']){let changed=false;for(const [key,value] of Object.entries(modbusDefaults(bus))){const el=f[key],different=Number(el.value)!==value;el.classList.toggle('modified-setting',different);el.setAttribute('aria-description',different?'Vom Modbus-Standard abweichend':'Modbus-Standard');changed||=different;}$(bus+'-modbus-note').hidden=!changed;}}
for(const bus of ['meter','house'])$(bus+'-modbus-reset').addEventListener('click',()=>{const f=$('config-form').elements;for(const [key,value] of Object.entries(modbusDefaults(bus)))f[key].value=String(value);updateModbus();toast('Modbus-Standard eingesetzt. Zum Übernehmen speichern.');});
function updateHouseQuery(){const f=$('config-form').elements,type=f.house_meter_type.value;
 const isTasmota=type==='tasmota',isShelly=type.startsWith('shelly_'),isEm24=type==='em24_tcp',network=isTasmota||isShelly||isEm24;
 const path=isTasmota?'/cm?cmnd=Status%2010':'',override=f.house_power_path.value.trim(),host=f.house_meter_host.value.trim();
 $('meter-discovery').hidden=!(isTasmota||isShelly);$('house-endpoint').hidden=!network;$('house-preview').hidden=!network;
 $('house-em24-note').hidden=!isEm24;
 $('house-host-label').textContent=isEm24?'EM24 IP-Adresse oder Hostname':'Adresse manuell eintragen';f.house_meter_host.placeholder=isEm24?'Zum Beispiel 192.168.178.20 oder em24.local':'Nur nötig, wenn die Suche nichts findet';
 $('meter-scan').textContent=type==='tasmota'?'Tasmota / Wattwächter im Heimnetz suchen':'Shelly-Zähler im Heimnetz suchen';
 if(scanTarget!=='house'||scanFamily!==meterFamily('house')){$('meter-found').hidden=true;$('meter-scan-status').textContent=meterScanning?'Vorherige Suche wird beendet …':'';}
 const key=JSON.stringify(previewRequest('house'));if(key!==previewKeys.house){$('house-preview-value').textContent='—';$('house-preview-state').textContent=host?'Messwert wird geprüft …':'Zähler auswählen oder IP eingeben.';}
 $('house-preview-host').textContent=host;
 $('house-endpoint').textContent=isEm24?(host?'Modbus TCP: '+(host.includes(':')?host:host+':502')+' · Adresse '+f.house_address.value:'EM24-IP-Adresse eingeben.'):isShelly?(host?'Modbus TCP: '+host+':502':'IP-Adresse eingeben oder Zähler suchen.'):(path?(override.startsWith('http://')||override.startsWith('https://')?'Abfrage: '+override:host?'Abfrage: http://'+host+path:'IP-Adresse eingeben oder Zähler suchen.'):'');
 $('house-query-help').textContent=isTasmota?'E320 und ENERGY werden automatisch erkannt. Eigenen Pfad nur bei anderem Skript eintragen.':type==='shelly_em1'?'Kanal 0, ohne Hochrechnung.':'Gesamtleistung und drei Phasenströme.';
}
function updateWallboxQuery(){const f=$('config-form').elements,isShelly=f.wallbox_meter_type.value.startsWith('shelly_'),isEm24=f.wallbox_meter_type.value==='em24_tcp',network=isShelly||isEm24,host=f.wallbox_meter_host.value.trim(),ui=previewUi('wallbox');
 $('wallbox-network-settings').hidden=!network;ui.box.hidden=!network;
 $('wallbox-meter-discovery').hidden=!isShelly;$('wallbox-shelly-modbus-note').hidden=!isShelly;$('wallbox-em24-note').hidden=!isEm24;
 $('wallbox-host-label').textContent=isEm24?'EM24 IP-Adresse oder Hostname':'Adresse manuell eintragen';f.wallbox_meter_host.placeholder=isEm24?'Zum Beispiel 192.168.178.21 oder IP:Port':'Nur nötig, wenn die Suche nichts findet';
 if(!network)return;
 if(scanTarget!=='wallbox'||scanFamily!=='shelly'){$('wallbox-meter-found').hidden=true;$('wallbox-meter-scan-status').textContent=meterScanning?'Vorherige Suche wird beendet …':'';}
 const key=JSON.stringify(previewRequest('wallbox'));if(key!==previewKeys.wallbox){ui.value.textContent='—';ui.state.textContent=host?'Messwert wird geprüft …':'Zähler auswählen oder IP eingeben.';}
 ui.host.textContent=host;
}
$('config-form').addEventListener('input',()=>{updateModbus();updateHouseQuery();updateWallboxQuery();});
$('house-query-reset').addEventListener('click',()=>{$('config-form').elements.house_power_path.value='';updateHouseQuery();toast('Automatische Abfrage gewählt. Zum Übernehmen speichern.');});
async function pollMeterScan(){try{if(document.hidden)return;const result=await api('/api/meters/scan',undefined,12000);meterScanning=result.running;meterResults=result.meters||[];scanFamily=result.family;
 const ui=scanUi(scanTarget);if(scanFamily!==meterFamily(scanTarget)){scanTarget==='house'?updateHouseQuery():updateWallboxQuery();return;}
 ui.status.textContent=result.running?`Suche im lokalen Heimnetz · ${result.progress} %`:`${meterResults.length} Zähler gefunden.${result.partial?' Suche vorzeitig beendet.':meterResults.length?'':' Adresse kann auch von Hand eingetragen werden. Bei Shelly muss Modbus TCP aktiviert sein.'}`;
 const select=ui.select,selected=select.value;const placeholder=document.createElement('option');placeholder.value='';placeholder.textContent='Zähler auswählen';select.replaceChildren(placeholder,...meterResults.map((r,i)=>{const option=document.createElement('option');option.value=String(i);const name=r.type==='tasmota'?'Tasmota / Wattwächter':r.type==='shelly_gen2'?'Shelly 3EM':'Shelly EM · Kanal 0';option.textContent=`${r.host} · ${name} · ${format(r.watts/1000,2)} kW`;return option;}));select.value=selected;select.hidden=!meterResults.length;
 }catch(error){meterScanning=false;scanUi(scanTarget).status.textContent='Suchstatus nicht erreichbar. Bitte erneut versuchen.';}finally{writes();if(meterScanning)setTimeout(pollMeterScan,2500);}}
function startMeterScan(role){action(async()=>{scanTarget=role;const family=meterFamily(role);await api('/api/meters/scan',{family});scanFamily=family;meterScanning=true;scanUi(role).status.textContent='Suche gestartet …';setTimeout(pollMeterScan,500);});}
$('meter-scan').addEventListener('click',()=>startMeterScan('house'));
$('wallbox-meter-scan').addEventListener('click',()=>startMeterScan('wallbox'));
function selectMeter(role){const ui=scanUi(role);if(ui.select.value===''||scanTarget!==role||scanFamily!==meterFamily(role))return;const meter=meterResults[Number(ui.select.value)];if(!meter)return;const f=$('config-form').elements;if(role==='wallbox'){f.wallbox_meter_type.value=meter.type;f.wallbox_meter_host.value=meter.host;}else{f.house_meter_type.value=meter.type;f.house_meter_host.value=meter.host;f.house_power_path.value='';}updateMeterFields();updateEquipment();previewKeys[role]='';previewTimes[role]=0;toast('Zähler ausgewählt. Zum Übernehmen speichern.');}
$('meter-found').addEventListener('change',()=>selectMeter('house'));
$('wallbox-meter-found').addEventListener('change',()=>selectMeter('wallbox'));

async function pollMeterPreview(){
 try{
  if(!online||!initialized||busy||uploading||document.hidden||$('settings').hidden||meterScanning||window.teennetFeatures?.networkBusy())return;
  if(previewRunning){
   const result=await api('/api/meters/preview',undefined,5000);previewRunning=result.running;const role=result.role||previewRole,request=previewRequest(role),key=JSON.stringify(request),ui=previewUi(role);
   if(JSON.stringify({role:result.role,host:result.host,type:result.type,path:result.path,unit_id:result.unit_id??1})!==key)return;
   if(!result.running){ui.value.textContent=result.ok?`${format(Math.abs(result.watts)/1000,2)} kW`:'—';ui.state.textContent=result.ok?role==='wallbox'?`Ladeleistung · vor ${Math.round(result.age_ms/1000)} s gemessen · Vorschau`:`${result.watts<0?'Einspeisung':result.watts>0?'Bezug':'Kein Leistungsfluss'} · vor ${Math.round(result.age_ms/1000)} s gemessen · Vorschau`:request.type==='em24_tcp'?'Kein gültiger EM24-Wert. IP, Port, Modbus-Adresse und Verbindung prüfen.':'Kein gültiger Messwert. Bei Shelly Modbus TCP, Modell, IP und Verbindung prüfen.';}
   return;
  }
  const roles=previewTurn++%2?['wallbox','house']:['house','wallbox'];let selected=null;
  for(const role of roles){const request=previewRequest(role),key=JSON.stringify(request),ui=previewUi(role),open=role==='house'?!$('house-settings').hidden&&$('house-settings').open:!$('meter-settings').hidden&&$('meter-settings').open;if(open&&!ui.box.hidden&&request.host&&(key!==previewKeys[role]||Date.now()-previewTimes[role]>=12000)){selected={role,request,ui};break;}}
  if(!selected)return;const {role,request,ui}=selected,key=JSON.stringify(request);previewRole=role;previewKeys[role]=key;previewTimes[role]=Date.now();ui.value.textContent='—';ui.state.textContent='Messwert wird geprüft …';await api('/api/meters/preview',request,5000);previewRunning=true;
 }catch(error){previewRunning=false;const ui=previewUi(previewRole);ui.value.textContent='—';ui.state.textContent=error.name==='AbortError'?'Zähler antwortet nicht rechtzeitig.':error.message;}
 finally{setTimeout(pollMeterPreview,1500);}
}
setTimeout(pollMeterPreview,1500);

function secretPlaceholders(){for(const name of ['wifi_password','mqtt_password','phase_shelly_password'])$('config-form').elements[name].placeholder=configBaseline[name+'_set']?'•••••••• · gespeichert':'Kein Passwort gespeichert';}
function passwordToggle(buttonName,inputName){$(buttonName).addEventListener('click',()=>{const input=$('config-form').elements[inputName],show=input.type==='password';input.type=show?'text':'password';$(buttonName).textContent=show?'Verbergen':'Anzeigen';$(buttonName).setAttribute('aria-pressed',String(show));});}
passwordToggle('show-mqtt-password','mqtt_password');
passwordToggle('show-wifi-password','wifi_password');
passwordToggle('show-phase-shelly-password','phase_shelly_password');

$('wifi-scan').addEventListener('click',()=>action(async()=>{const button=$('wifi-scan');button.textContent='Suche …';try{const result=await api('/api/wifi/scan');const select=$('wifi-networks'),unique=[...new Map((result.networks||[]).map(n=>[n.ssid,n])).values()];const placeholder=document.createElement('option');placeholder.value='';placeholder.textContent='Netzwerk auswählen …';select.replaceChildren(placeholder,...unique.map(n=>{const option=document.createElement('option');option.value=n.ssid;option.textContent=`${n.ssid} (${n.rssi} dBm)`;return option;}));select.hidden=!unique.length;toast(`${unique.length} WLAN-Netzwerke gefunden.`);}finally{button.textContent='Netzwerke suchen';}}));

$('wifi-networks').addEventListener('change',()=>{if($('wifi-networks').value)$('config-form').elements.wifi_ssid.value=$('wifi-networks').value;});

$('ap-mode').addEventListener('click',()=>{if(!window.confirm('Heimnetz trennen und in den Einrichtungs-AP wechseln? Einstellungen bleiben gespeichert.'))return;action(async()=>{await api('/api/wifi/ap',{});toast('Mit Wallbox-EMS-Setup verbinden und 192.168.4.1 öffnen.');});});

$('battery-toggle').addEventListener('click',()=>action(async()=>{if(!state.battery_protect){toast('Akku-Daten zuerst unter Einstellungen aktivieren.',true);return;}await api('/api/control',{battery_use:!state.battery_use});await pollStatus();}));
$('battery-start-toggle').addEventListener('click',()=>action(async()=>{if(!state.battery_protect){toast('Akku-Daten zuerst unter Einstellungen aktivieren.',true);return;}await api('/api/control',{battery_start_use:!state.battery_start_use});await pollStatus();}));
$('battery-reserve-slider').addEventListener('input',()=>{batterySliderDirty=true;updateBatterySlider();});
function applyReserveOnRelease(){if($('mode').value!=='pv'){reserveQueued=false;return;}if(!reserveQueued||busy||!online)return;action(async()=>{reserveQueued=false;const reserve=Number($('battery-reserve-slider').value);const result=await api('/api/config',{battery_reserve_soc:reserve});if(updateSaveStatus(result))throw new Error('Gespeichert. Bitte neu starten, damit die Grenze gilt.');const field=$('config-form').elements.battery_reserve_soc;if(Number(field.value)===configBaseline.battery_reserve_soc)field.value=String(reserve);configBaseline.battery_reserve_soc=reserve;batterySliderDirty=false;await pollStatus();toast(`Entladegrenze ${reserve} % gespeichert.`);});}
$('battery-reserve-slider').addEventListener('change',()=>{reserveQueued=true;applyReserveOnRelease();});
$('control-form').addEventListener('input',()=>{dirty=true;updateDesiredPower();});
$('current').addEventListener('change',()=>{powerQueued=true;applyPowerOnRelease();});
function applyPowerOnRelease(){if(!powerQueued||busy||!online||$('mode').value!=='manual')return;action(async()=>{do{powerQueued=false;const current=selectedCurrent(),enabled=selectedPower()>0;await api('/api/control',{mode:'manual',current_a:current,manual_phases:phaseForPower(selectedPower()),enabled});rememberChargeMode('manual');}while(powerQueued&&online&&$('mode').value==='manual');dirty=false;await pollStatus();toast('Ladeleistung übernommen.');});}

document.querySelectorAll('[data-mode]').forEach(el=>el.addEventListener('click',()=>{
  if(busy||!online)return;
  if(el.dataset.mode==='pv'&&(state?.basic_mode||state?.zero_feed_enabled===false))return;
  const mode=el.dataset.mode;$('mode').value=mode;if(mode!=='manual')powerQueued=false;
  dirty=true;render();
  // A running session changes policy immediately; only actual phase changes
  // and missing operating prerequisites require a stop in the controller.
  if(state?.enabled&&state.mode!==mode)action(async()=>{try{await api('/api/control',{mode});rememberChargeMode(mode);dirty=false;await pollStatus();toast('Betriebsart übernommen.');}catch(error){dirty=false;render();throw error;}});
  else{rememberChargeMode(mode);dirty=false;render();}
}));

$('control-form').addEventListener('submit',e=>{e.preventDefault();action(async()=>{
  if(state.phase_test_active){await api('/api/phase-test',{stop:true});toast('Schütztest beendet. Ladung bleibt gestoppt.');}
  else if(state.enabled||state.evcc_enabled){rememberChargeMode($('mode').value);await api('/api/control',{enabled:false,mode:'off'});toast('Stop-Anforderung gesendet.');}
  else if(state.charge_stop_latched||state.phase_state==='fehler'){
    if(state.can_unlock===false){toast('Zuerst Störung beheben.',true);return;}
    await api('/api/control',{enabled:false,restart:true});toast('Sperre aufgehoben. Zum Laden erneut starten.');
  }else {
    const actual=state.actual_a||[];
    if(state.feedback_ok&&actual.some(a=>Number.isFinite(a)&&a>=1)){toast('Stopp wird noch ausgeführt.');return;}
    const mode=$('mode').value,enabled=mode!=='manual'||selectedPower()>0;
    await api('/api/control',{mode,current_a:selectedCurrent(),manual_phases:phaseForPower(selectedPower()),enabled,restart:enabled});
    rememberChargeMode(mode);
    toast(enabled?'Start angefordert.':'Zuerst eine Ladeleistung auswählen.');
  }
  dirty=false;await pollStatus();
});});
$('unlock-control').addEventListener('click',()=>action(async()=>{await api('/api/control',{enabled:false,restart:true});await pollStatus();toast('Sperre aufgehoben. Mit Start laden.');}));


function validateConfigForm(){
 // Expert fields have their own save actions; optional hidden controls are not part of this form's validation.
 for(const input of $('config-form').elements){
  if(!input.name||input.disabled||input.closest('#expert-settings')||input.closest('[hidden]')||input.checkValidity())continue;
  const label=(input.closest('label')?.firstChild?.textContent||input.name).trim();
  configSaveError=`${label}: ${input.validationMessage}`;updateUnsavedFunctions();
  for(let parent=input.parentElement;parent;parent=parent.parentElement)if(parent.tagName==='DETAILS')parent.open=true;
  input.focus();input.reportValidity();return false;
 }
 return true;
}
$('config-form').addEventListener('submit',e=>{e.preventDefault();if(!validateConfigForm())return;action(async()=>{
 const data={};configSaveError='';
 for(const el of $('config-form').elements){
  if(!el.name||el.type==='submit'||el.disabled||el.closest('#expert-settings'))continue;
  if(el.hasAttribute('data-secret')&&!el.value)continue;
  const scale=Number(el.dataset.scale)||1;
  const value=el.type==='number'||el.hasAttribute('data-number')?Number(el.value)*scale:el.type==='checkbox'?el.checked:el.hasAttribute('data-boolean')?el.value==='true':el.value;
  const baseline=typeof configBaseline[el.name]==='number'?Number(configBaseline[el.name].toFixed(4)):configBaseline[el.name];
  if(value!==baseline)data[el.name]=value;
 }
 if($('clear-wifi-password').checked)data.wifi_password='';
 if(Number($('config-form').elements.phase_actuator_type.value)===1&&$('phase-shelly-auth').value==='0'&&configBaseline.phase_shelly_password_set)data.phase_shelly_password='';
 configSaving=true;writes();
 try{
  let result;
  try{result=await api('/api/config',data);}
  catch(error){configSaveError=error.name==='AbortError'?'Zeitüberschreitung. Verbindung prüfen und erneut speichern.':error.message;updateUnsavedFunctions();throw error;}
  Object.assign(configBaseline,data);
  for(const name of ['wifi_password','mqtt_password','phase_shelly_password'])if(name in data){configBaseline[name+'_set']=!!data[name];delete configBaseline[name];}
  secretPlaceholders();await loadConfig();$('clear-wifi-password').checked=false;
  $('config-note').hidden=true;for(const el of $('config-form').elements)if(el.hasAttribute('data-secret'))el.value='';
  const restartNeeded=updateSaveStatus(result);updateUnsavedFunctions();
  if(!restartNeeded)toast(settingsRebootPending?'Änderungen übernommen. Ein früherer Neustart steht noch aus.':'Einstellungen übernommen.');
  await pollStatus();
 }finally{configSaving=false;writes();}
});});



function restartController(){action(async()=>{await api('/api/reboot',{});toast('Zählerstand wird gesichert. Controller startet neu.');initialized=false;dirty=false;settingsRebootPending=false;updateUnsavedFunctions();showConnection(false);});}
$('reboot').addEventListener('click',restartController);
$('settings-notice-reboot').addEventListener('click',restartController);
$('settings-notice-later').addEventListener('click',()=>{settingsRebootDeferred=true;updateUnsavedFunctions();});

$('factory-reset').addEventListener('click',()=>{if(!window.confirm('Alle WLAN-, MQTT-, Einstellungs- und Statistikdaten löschen?'))return;action(async()=>{await api('/api/factory-reset',{});toast('Werksreset wird ausgeführt.');initialized=false;dirty=false;showConnection(false);});});

$('reset-total').addEventListener('click',()=>{if(!window.confirm('Gesamtzähler auf 0 kWh setzen? Ladesitzungen und Tageswerte bleiben erhalten.'))return;action(async()=>{await api('/api/energy/reset',{});await pollStatus();toast('Gesamtzähler zurückgesetzt.');});});
$('reset-sessions').addEventListener('click',()=>{if(!window.confirm('Alle gespeicherten Ladesitzungen und den CSV-Inhalt löschen?'))return;action(async()=>{await api('/api/sessions/reset',{});sessionOffset=0;await loadSessions();toast('Ladesitzungen gelöscht.');});});

async function stopForMaintenance(){
  if(state.enabled)await api('/api/control',{enabled:false,mode:'off'});
  const deadline=Date.now()+60000;
  while(true){await pollStatus();const a=state.actual_a||[];if(!state.enabled&&(!state.feedback_ok||a.length===3&&a.every(v=>Number.isFinite(v)&&v<1)))return;if(Date.now()>deadline)throw Error('Ladestopp noch nicht bestätigt.');await new Promise(resolve=>setTimeout(resolve,1000));}
}
async function downloadSystemBackup(){
  $('github-status').textContent='Systemsicherung wird gelesen …';
  const controller=new AbortController(),timeout=setTimeout(()=>controller.abort(),300000);
  try{
    const response=await fetch('/api/backup/system',{headers:{'X-EMS-Token':token},cache:'no-store',signal:controller.signal});
    if(!response.ok)throw Error((await response.text()).slice(0,240)||'Sicherung fehlgeschlagen.');
    const expected=Number(response.headers.get('X-TeeNet-Backup-Bytes'));
    if(!Number.isInteger(expected)||expected<4*1024*1024||expected>32*1024*1024)throw Error('Keine vollständige Systemsicherung erhalten.');
    const reader=response.body.getReader(),chunks=[];let received=0;
    while(true){const next=await reader.read();if(next.done)break;received+=next.value.length;if(received>expected){await reader.cancel();throw Error('Sicherungsgröße stimmt nicht.');}chunks.push(next.value);$('github-status').textContent=`Systemsicherung: ${Math.round(received/expected*100)} %`;}
    if(received!==expected)throw Error('Sicherung unvollständig. Update nicht gestartet.');
    const zip=await TeeNetFirmware.backupZip(new Blob(chunks),state);
    const url=URL.createObjectURL(zip),link=document.createElement('a');link.href=url;link.download=`TeeNet-${state.version}-Systemsicherung-${new Date().toISOString().slice(0,10)}.zip`;document.body.append(link);link.click();link.remove();setTimeout(()=>URL.revokeObjectURL(url),60000);
    $('github-status').textContent='Sicherung heruntergeladen.';
  }finally{clearTimeout(timeout);}
}
$('system-backup').addEventListener('click',()=>action(async()=>{
  if(!window.confirm('Ladung stoppen und komplettes System sichern? Die Datei enthält Passwörter. Privat aufbewahren.'))return;
  uploading=true;try{await stopForMaintenance();await downloadSystemBackup();}finally{uploading=false;}
}));
async function waitGitHub(){
  const deadline=Date.now()+240000;
  while(true){const result=await api('/api/update/github',undefined,15000);$('github-status').textContent=result.message+(result.running&&result.installing?` · ${result.progress} %`:'');
    if(!result.running){if(!result.ok)throw Error(result.message||'GitHub-Abfrage fehlgeschlagen.');return result;}
    if(Date.now()>deadline)throw Error('Zeitüberschreitung. Update-Status erneut prüfen.');await new Promise(resolve=>setTimeout(resolve,1500));}
}
let githubRelease=null;
$('github-check').addEventListener('click',()=>action(async()=>{
  githubRelease=null;$('github-install').hidden=true;
  await api('/api/update/github',{action:'check'});const result=await waitGitHub();githubRelease=result;window.dispatchEvent(new CustomEvent('teenet-release',{detail:{version:result.version}}));
  $('github-install').hidden=!result.newer;$('github-install').textContent=`TeeNet ${result.version} installieren`;
  $('github-status').textContent=result.newer?`TeeNet ${result.version} verfügbar.`:`GitHub: TeeNet ${result.version}. Installierte Version ${state.version} ist aktuell oder neuer.`;
}));
$('github-install').addEventListener('click',()=>action(async()=>{
  if(!githubRelease?.newer)throw Error('Zuerst neueste Version prüfen.');
  if(!window.confirm(`TeeNet ${githubRelease.version} von GitHub installieren? Ladung wird gestoppt.${$('ota-backup').checked?' Die Sicherung enthält Passwörter.':''}`))return;
  uploading=true;let updateStarted=false;
  try{
    await stopForMaintenance();if($('ota-backup').checked)await downloadSystemBackup();
    await api('/api/update/github',{action:'install'});updateStarted=true;
    TeeNetFirmware.remember({version:githubRelease.version},state.build_id);
    await waitGitHub();initialized=false;showConnection(false);schedulePoll(2000);
  }catch(error){if(!updateStarted){$('github-status').textContent=error.message;}else {$('github-status').textContent=error.message+' Version und Verbindung prüfen.';schedulePoll(2000);}throw error;}
  finally{uploading=false;}
}));
$('ota-upload').addEventListener('click',()=>{
 if(busy||uploading||!online)return;
 $('ota-file').value='';$('ota-file').click();
});
$('ota-file').addEventListener('change',()=>{
 const file=$('ota-file').files?.[0];if(!file)return;
 action(async()=>{
  const info=await TeeNetFirmware.inspect(file,state.ota_max_bytes||2097152);
  if(document.documentElement.dataset.demo==='true'){$('github-status').textContent=`Demo: TeeNet ${info.version} erkannt. Datei geprüft; es wird keine Firmware installiert.`;$('ota-file').value='';return;}
  const older=TeeNetFirmware.compare(info.version,state.version)<0?' Dies ist eine ältere Version.':'';
  if(!window.confirm(`TeeNet ${info.version} aus „${file.name}“ installieren?${older} Die Datei muss zu ${state.hardware||'diesem Board'} passen. Ladung wird gestoppt; danach startet TeeNet neu.${$('ota-backup').checked?' Die Sicherung enthält Passwörter.':''}`))return;
  uploading=true;let accepted=false;
  try{
   await stopForMaintenance();if($('ota-backup').checked)await downloadSystemBackup();
   $('github-status').textContent='Lokale Firmware wird übertragen …';
   await new Promise((resolve,reject)=>{
    const xhr=new XMLHttpRequest();xhr.open('POST','/api/ota');xhr.timeout=210000;
    xhr.setRequestHeader('Content-Type','application/octet-stream');xhr.setRequestHeader('X-EMS-Token',token);
    xhr.upload.onprogress=e=>{if(e.lengthComputable)$('github-status').textContent=`Lokale Firmware übertragen: ${Math.round(e.loaded/e.total*100)} %`;};
    xhr.onload=()=>{if(xhr.status>=200&&xhr.status<300)resolve();else reject(Error(xhr.responseText.slice(0,240)||'Firmware wurde nicht angenommen.'));};
    xhr.onerror=()=>reject(Error('Übertragung unterbrochen. Verbindung und installierte Version prüfen.'));
    xhr.ontimeout=()=>reject(Error('Zeitüberschreitung. Verbindung und installierte Version prüfen.'));
    xhr.send(file);
   });
   accepted=true;TeeNetFirmware.remember(info,state.build_id);
   $('github-status').textContent='Firmware angenommen. TeeNet startet neu …';
   initialized=false;showConnection(false);schedulePoll(2000);
  }catch(error){$('github-status').textContent=error.message;throw error;}
  finally{uploading=false;$('ota-file').value='';if(accepted)schedulePoll(2000);}
 });
});
let statusPromise=null,loadedBuild='';
async function pollStatus(){if(statusPromise)return statusPromise;statusPromise=(async()=>{const next=await api('/api/status',undefined,12000);if(loadedBuild&&next.build_id&&loadedBuild!==next.build_id){window.location.reload();return;}loadedBuild=next.build_id||loadedBuild;window.TeeNetHealth?.observe(next);state=next;settingsRebootPending=!!next.reboot_required;if(!settingsRebootPending)settingsRebootDeferred=false;updateUnsavedFunctions();token=state.token;statusFailures=0;showConnection(true);render();const updateMessage=window.TeeNetFirmware?.checkBoot(next);if(updateMessage){$('github-status').textContent=updateMessage;toast(updateMessage);}})();try{return await statusPromise;}finally{statusPromise=null;}}

async function loadHistory(){if(historyBusy||!online||document.hidden)return;historyBusy=true;lastHistoryAt=Date.now();try{history=await api('/api/history',undefined,15000);drawCharts();}catch(error){}finally{historyBusy=false;}}

let lastHistoryAt=-Infinity,lastSessionsAt=-Infinity;

let pollTimer=null,pollRunning=false;
function pollDelay(){
  if(!state||state.enabled!==false||!state.feedback_ok||!Array.isArray(state.actual_a)||state.actual_a.some(a=>!Number.isFinite(a)||a>=1))return 2000;
  return !$('settings').hidden?5000:10000;
}
function schedulePoll(delay=2000){clearTimeout(pollTimer);if(!document.hidden)pollTimer=setTimeout(poll,delay);}
async function poll(){if(pollRunning||document.hidden)return;pollRunning=true;try{if(busy||uploading)return;await pollStatus();if(!initialized){await loadConfig();initialized=true;updateUnsavedFunctions();writes();}if(!$('statistics').hidden){const now=Date.now();if(now-lastHistoryAt>=60000)loadHistory();if(now-lastSessionsAt>=30000)loadSessions();}}catch(error){if(++statusFailures>=2){showConnection(false);render();}}finally{pollRunning=false;schedulePoll(statusFailures?Math.min(30000,2000*2**Math.min(statusFailures,4)):pollDelay());}}
document.addEventListener('visibilitychange',()=>{clearTimeout(pollTimer);if(!document.hidden){lastHistoryAt=lastSessionsAt=-Infinity;schedulePoll(0);}});

function updateHeaderOffset(){
  const header=document.querySelector('.app-header');
  if(!header)return;
  const height=getComputedStyle(header).position==='sticky'?Math.ceil(header.getBoundingClientRect().height)+16:24;
  document.documentElement.style.setProperty('--header-offset',`${height}px`);
}
window.addEventListener('resize',()=>{drawCharts();updateHeaderOffset();});
if(typeof ResizeObserver!=='undefined')new ResizeObserver(updateHeaderOffset).observe(document.querySelector('.app-header'));
updateHeaderOffset();

function showPage(){const anchor=location.hash.slice(1),groups=['equipment-settings','opendtu-settings','huawei-settings','relay-settings','connection-settings','shell-settings','meter-settings','house-settings','hardware-settings','consumption-settings','mqtt-settings','evcc-settings','vehicle-settings','diagnostic-settings','maintenance','appearance-settings','expert-settings'];const page=anchor==='statistics'?'statistics':anchor==='settings'||groups.includes(anchor)?'settings':'overview';for(const id of ['overview','statistics','settings'])$(id).hidden=id!==page;document.querySelector('.status-wrap').hidden=page!=='overview';document.querySelectorAll('nav a').forEach(a=>{const active=a.getAttribute('href')==='#'+page;a.classList.toggle('active',active);if(active)a.setAttribute('aria-current','page');else a.removeAttribute('aria-current');});if(page==='statistics'&&initialized){loadHistory();loadSessions();}if(groups.includes(anchor)){for(let section=$(anchor);section;section=section.parentElement)if(section.tagName==='DETAILS')section.open=true;}requestAnimationFrame(()=>{drawCharts();if(groups.includes(anchor))$(anchor).scrollIntoView({block:'start'});else window.scrollTo(0,0);});}

const donateUrl=$('donate-link').href;
let qrLoading=false;
function showDonate(){
  $('donate-dialog').showModal();
  if(qrLoading||$('donate-qr').querySelector('svg'))return;
  qrLoading=true;
  const script=document.createElement('script');script.src='/qrcode.js';
  script.onload=()=>{try{const qr=qrcode(0,'M');qr.addData(donateUrl);qr.make();$('donate-qr').innerHTML=qr.createSvgTag({cellSize:5,margin:4,scalable:true});}catch(error){$('donate-qr').textContent='QR-Code konnte nicht erzeugt werden.';}finally{qrLoading=false;}};
  script.onerror=()=>{qrLoading=false;$('donate-qr').textContent='QR-Code konnte nicht geladen werden.';};
  document.head.append(script);
}
$('donate-open').addEventListener('click',showDonate);
$('donate-close').addEventListener('click',()=>$('donate-dialog').close());
$('donate-dialog').addEventListener('click',e=>{if(e.target===$('donate-dialog'))$('donate-dialog').close();});
$('theme-light').addEventListener('click',()=>selectTheme('light'));
$('theme-dark').addEventListener('click',()=>selectTheme('dark'));
setTheme(document.documentElement.dataset.theme);
loadDeviceTheme();

window.addEventListener('hashchange',showPage);

showPage();writes();poll();

/* Separate Shelly actuator setup; meter searches keep their own results. */
(() => {
 let scanning=false,devices=[],pollTimer,lastBaseline;
 const f=$('config-form').elements,select=$('phase-shelly-found'),note=$('phase-shelly-scan-status'),feedback=$('phase-feedback-choice'),connection=$('phase-shelly-connection');
 function update(){
  const kind=Number(f.phase_actuator_type.value),remote=kind===1||kind===3,mqtt=kind===2;
  const baseline=typeof configBaseline==='undefined'?null:configBaseline;
  if(baseline&&baseline!==lastBaseline){$('phase-shelly-auth').value=baseline.phase_shelly_password_set?'1':'0';connection.value=Number(baseline.phase_actuator_type)===3?'3':'1';feedback.value=f.phase_feedback_count.value==='2'?'2':f.phase_feedback_closed_is_single.value==='true'?'1-single':'1';$('phase-choice-note').hidden=true;$('phase-choice-note').textContent='';lastBaseline=baseline;}
  if(!connection.value)connection.value=kind===3?'3':'1';
  if(!feedback.value)feedback.value='1';
  const none=mqtt;

  for(const option of Array.from(f.phase_shelly_model.options||[]))option.disabled=false;
  const modbusOption=Array.from(connection.options||[]).find(option=>option.value==='3');
  if(modbusOption)modbusOption.disabled=!['4','12'].includes(f.phase_shelly_model.value);
  const inputs=[2,2,1,1,1,2,2,2,3,4,1,1,1][Number(f.phase_shelly_model.value)]||1;

  for(const option of Array.from(feedback.options||[]))option.disabled=remote&&((option.value==='2'&&inputs===1)||(option.value!=='2'&&f.phase_shelly_pulse.value==='true'));
  for(const option of Array.from(f.phase_shelly_pulse.options||[]))option.disabled=option.value==='true'&&(kind===3||inputs===1);
  $('phase-shelly-settings').hidden=!remote||none;
  $('phase-actuator-label').hidden=none&&!!state?.phase_mqtt_allowed;
  $('phase-feedback-settings').hidden=none;
  $('phase-feedback-heading').textContent=remote?'Rückmeldung über Shelly':'Rückmeldung am ESP';
  $('phase-feedback-description').textContent=remote?'Hilfskontakte des Schützes an die Shelly-Eingänge anschließen.':'Potentialfreie Hilfskontakte des Schützes an die ESP-Eingänge anschließen.';
  f.phase_feedback_enabled.closest('label').hidden=true;
  $('phase-connection-note').hidden=remote||none;
  f.phase_shelly_host.required=remote&&!none&&f.phase_switch_enabled.checked;
  /* Previewing the protected option must not dirty ordinary saved settings. */
  f.phase_feedback_enabled.checked=!mqtt;f.relay1_mode.value=f.phase_switch_enabled.checked?'3':'0';
  const password=$('phase-shelly-auth').value==='1';
  $('phase-shelly-auth-settings').hidden=kind===3;
  $('phase-shelly-password-label').hidden=!password;
  f.phase_shelly_password.disabled=kind===3||!password;
  f.phase_shelly_password.required=kind===1&&!none&&f.phase_switch_enabled.checked&&password&&!baseline?.phase_shelly_password_set;
  $('phase-shelly-polarity').hidden=f.phase_shelly_pulse.value==='true';
  const pulseNote=$('phase-pulse-support');
  pulseNote.hidden=!remote||none;
  pulseNote.textContent=kind===3?'Modbus: nur Ein/Aus. Für Impulse Pro 1 oder Pro 2 über HTTP wählen.':
   inputs===1?'Dieses Modell hat einen Rückmeldeeingang. Impulse benötigen Pro 1 oder Pro 2 mit zwei Kontakten.':
   f.phase_shelly_pulse.value==='true'?'TeeNet startet den Impuls. Der Shelly schaltet nach 0,5 s aus. Zwei Rückmeldekontakte erforderlich.':'';
  pulseNote.hidden=!remote||none||!pulseNote.textContent;
  const gp={relay1_pin:12,phase_feedback_pin:13,phase_feedback_second_pin:14,...state?.gpio_pins,...baseline};
  $('phase-connection-note').innerHTML=`GPIO${gp.relay1_pin}: Relais · 3,3 V / High schaltet ein · GPIO${gp.phase_feedback_pin}: Kontakt 1${Number(f.phase_feedback_count.value)===2?` · GPIO${gp.phase_feedback_second_pin}: Kontakt 2`:''}. <a href="/guide#relais-phasen" target="_blank" rel="noopener">Anschluss</a>`;
  const mapping=f.phase_feedback_closed_is_single;
  $('phase-feedback-single-mapping').hidden=true;
  const closedSingle=mapping.value==='true',closedText=closedSingle?'einphasig':'dreiphasig',openText=closedSingle?'dreiphasig':'einphasig';
  const pins=$('phase-feedback-pin-note');pins.hidden=none;
  pins.textContent=remote?(Number(f.phase_feedback_count.value)===2?
   'SW1 (Eingang 0) geschlossen: dreiphasig.\nSW2 (Eingang 1) geschlossen: einphasig.\nGenau ein Kontakt muss geschlossen sein.':
   `SW1 (Eingang 0) geschlossen: ${closedText}.\nOffen: ${openText}. Eingang nicht invertieren.`):
   Number(f.phase_feedback_count.value)===2?
   `GPIO${gp.phase_feedback_pin} gegen GND geschlossen: dreiphasig. GPIO${gp.phase_feedback_second_pin} gegen GND geschlossen: einphasig. Genau ein Kontakt muss geschlossen sein.`:
   `GPIO${gp.phase_feedback_pin} gegen GND geschlossen: ${closedText}. Offen: ${openText}. Nur potentialfreie Kontakte verwenden.`;
  $('phase-shelly-setup-note').textContent=kind===3?'Am Shelly: Modbus aktivieren, Eingang entkoppeln (Detached), Einschaltzustand Aus, Timer aus. HTTP bleibt für die Prüfung nötig.':
   f.phase_shelly_pulse.value==='true'?'Am Shelly: Eingang entkoppeln (Detached), Einschaltzustand Aus, Auto-Aus 0,5 s; Auto-Ein aus.':
   'Am Shelly: Eingang entkoppeln (Detached), Einschaltzustand Aus, Timer aus.';
  const option=Array.from(f.phase_actuator_type.options||[]).find(o=>o.value==='2');
  if(option)option.hidden=!mqtt&&!state?.phase_mqtt_allowed;
  updateLive();
 }
 /* Status polling must not rewrite unsaved configuration fields. */
 function updateLive(){
  const kind=Number(f.phase_actuator_type.value),none=kind===2;
  $('phase-actuator-label').hidden=none&&!!state?.phase_mqtt_allowed;
  const outputNote=$('phase-mqtt-profile-note');outputNote.hidden=!none;
  if(none)outputNote.textContent=state?.phase_mqtt_allowed?
   `${['MQTT','Shelly Modbus TCP','Shelly HTTP ohne Passwort','Shelly HTTP mit Passwort'][state.phase_output_transport||0]}: ${typeof state.phase_mqtt_output_single==='boolean'?`letzter Befehl ${state.phase_mqtt_output_single?'einphasig':'dreiphasig'}`:'noch kein Befehl bestätigt'} · keine Schützrückmeldung.`:
   'Ohne Rückmeldung: Freigabe im Expertenmodus erforderlich.';
  if(none){const link=document.createElement('a');link.href='#expert-settings';link.textContent=' Im Expertenmodus ändern';outputNote.append(link);}
  const contacts=$('phase-contact-status');
  contacts.hidden=none||!state?.phase_switch_enabled||kind!==state.phase_actuator_type;
  if(!contacts.hidden){
   const baseline=typeof configBaseline==='undefined'?null:configBaseline;
   const changed=baseline&&['phase_feedback_count','phase_feedback_closed_is_single','phase_shelly_model','phase_shelly_pulse','phase_shelly_true_three_phase','phase_shelly_host'].some(name=>String(baseline[name])!==f[name].value);
   if(changed||state.reboot_required){contacts.textContent=state.reboot_required?'Rückmeldung nach dem Neustart prüfen.':'Neue Zuordnung noch nicht gespeichert.';return;}
   const contactText=value=>typeof value==='boolean'?(value?'geschlossen':'offen'):'unbekannt';
   const observed=Number(state.phase_feedback);
   contacts.textContent=`Kontakt 1: ${contactText(state.phase_contact1_closed)}${Number(state.phase_feedback_count)===2?` · Kontakt 2: ${contactText(state.phase_contact2_closed)}`:''} · ${observed===1?'Einphasig bestätigt':observed===3?'Dreiphasig bestätigt':'Schützstellung nicht bestätigt'}`;
  }
 }
 function render(result){
  if(result.family!=='shelly_switch')throw Error('Andere Gerätesuche läuft. Bitte später erneut versuchen.');
  devices=result.devices||[];select.replaceChildren();
  const first=document.createElement('option');first.value='';first.textContent='Shelly auswählen';select.append(first);
  devices.forEach((device,index)=>{
   const option=document.createElement('option');option.value=String(index);option.disabled=!device.compatible;
   option.textContent=`${device.host} · ${device.name}${device.protected?' · Passwortschutz':''}${device.compatible?' · Hardware noch nicht getestet':' · nicht unterstützt'}`;
   select.append(option);
  });select.hidden=!devices.length;
  note.textContent=result.running?`Suche nach Shelly-Schaltgeräten · ${result.progress} %`:
   `${devices.filter(d=>d.compatible).length} passende Shelly gefunden.${result.partial?' Suche vorzeitig beendet.':''} ${devices.some(d=>d.protected)?'Geschützte Geräte werden angezeigt; Passwort unten eingeben.':''}`;
 }
 async function poll(){
  try{const result=await api('/api/meters/scan');render(result);if(result.running){pollTimer=setTimeout(poll,1500);return;}}
  catch(error){note.textContent=error.message;}
  scanning=false;$('phase-shelly-scan').disabled=!online||busy;
 }
 $('phase-shelly-scan').addEventListener('click',async()=>{
  if(scanning||!online||busy)return;
  scanning=true;$('phase-shelly-scan').disabled=true;clearTimeout(pollTimer);
  try{const result=await api('/api/meters/scan',{family:'shelly_switch'});render(result);if(result.running)pollTimer=setTimeout(poll,1500);else scanning=false;}
  catch(error){scanning=false;note.textContent=error.message;}
  if(!scanning)$('phase-shelly-scan').disabled=!online||busy;
 });
 select.addEventListener('change',()=>{
  const device=devices[Number(select.value)];if(select.value===''||!device?.compatible)return;
  f.phase_shelly_host.value=device.host;
  if(Number.isInteger(device.profile)&&device.profile<=12)f.phase_shelly_model.value=String(device.profile);
  $('phase-shelly-auth').value=device.protected?'1':'0';normalizeChoice();update();
  f.phase_shelly_host.dispatchEvent(new Event('change',{bubbles:true}));
 });
 // Normalize only explicit user choices, never saved data while rendering or polling.
 function normalizeChoice(event){
  let kind=Number(f.phase_actuator_type.value),remote=kind===1||kind===3;
  const notice=$('phase-choice-note');notice.textContent='';notice.hidden=true;
  if(event?.target===connection){kind=Number(connection.value);f.phase_actuator_type.value=String(kind);remote=kind===1||kind===3;}
  if((event?.target===connection||event?.target?.name==='phase_actuator_type')&&kind===3){if(!['4','12'].includes(f.phase_shelly_model.value))f.phase_shelly_model.value='4';notice.textContent='Modbus TCP gewählt: Gen4-Shelly mit einem Rückmeldekontakt.';}
  if(kind===3&&!['4','12'].includes(f.phase_shelly_model.value)){f.phase_actuator_type.value='1';connection.value='1';kind=1;notice.textContent='Dieses Modell unterstützt hier HTTP. Die Verbindung wurde auf HTTP gesetzt.';}
  else connection.value=kind===3?'3':'1';
  const inputs=[2,2,1,1,1,2,2,2,3,4,1,1,1][Number(f.phase_shelly_model.value)]||1;
  if(remote&&(kind===3||inputs===1))f.phase_shelly_pulse.value='false';
  if(remote&&inputs===1)f.phase_feedback_count.value='1';
  if(remote&&f.phase_shelly_pulse.value==='true')f.phase_feedback_count.value='2';
  if(f.phase_feedback_count.value==='2')f.phase_feedback_closed_is_single.value='false';
  feedback.value=f.phase_feedback_count.value==='2'?'2':f.phase_feedback_closed_is_single.value==='true'?'1-single':'1';
  notice.hidden=!notice.textContent;
 }
 for(const name of ['phase_actuator_type','phase_shelly_model','phase_shelly_pulse'])f[name].addEventListener('change',normalizeChoice);
 connection.addEventListener('change',event=>{normalizeChoice(event);f.phase_actuator_type.dispatchEvent(new Event('change',{bubbles:true}));});
 $('phase-shelly-auth').addEventListener('change',update);
 window.teenetShellyPhase={update,updateLive};update();
})();
/* Password stays in this page's memory; no cookies or local storage. */
(() => {
 let pin='',snapshot=null,working=false,timer,rows=[],savedModified=false,lastMarkerState=null,expertSchemaPromise=null;
 const el=id=>document.getElementById(id),same=(a,b)=>typeof a==='number'&&typeof b==='number'?Math.abs(a-b)<.0001:a===b;
 const message=(text,error=false)=>{el('expert-message').textContent=text;el('expert-message').classList.toggle('expert-message-error',error);};
 function parameterMeta(raw){const meta={...raw};for(const key of ['min','max','step','default','value','active'])if(typeof meta[key]==='number')meta[key]=Number(meta[key].toFixed(6));return meta;}
 async function expertSchema(){
  if(!expertSchemaPromise)expertSchemaPromise=fetch('/expert-schema.json',{cache:'no-cache'}).then(async response=>{if(!response.ok)throw Error('Expertenparameter konnten nicht geladen werden.');return response.json();}).catch(error=>{expertSchemaPromise=null;throw error;});
  return expertSchemaPromise;
 }
 async function loadExpertParameters(accessPin){
  const dynamic=await api('/api/expert',{pin:accessPin,action:'read'});
  /* Full objects remain accepted by isolated UI tests and older previews. */
  if(Array.isArray(dynamic.parameters))return dynamic;
  const schema=await expertSchema(),values=dynamic.values,active=dynamic.active;
  if(!Array.isArray(schema.parameters)||!Array.isArray(values)||!Array.isArray(active)||values.length!==schema.parameters.length||active.length!==values.length)throw Error('Expertenparameter unvollständig.');
  return {parameters:schema.parameters.map((meta,index)=>({...meta,value:values[index],active:active[index]})),settings:configBaseline||{},pending:!!dynamic.pending,
   phase_mqtt_allowed:state?.phase_mqtt_allowed,phase_mqtt_ready:state?.phase_mqtt_ready,phase_output_base_ready:state?.phase_output_base_ready,
   phase_output_meter_ready:state?.phase_output_meter_ready,phase_output_measured_phases:state?.phase_output_measured_phases,phase_output_mqtt_ready:state?.phase_output_mqtt_ready};
 }
 async function loadExpertGPIO(accessPin){return api('/api/expert',{pin:accessPin,action:'read_gpio'});}
 function readable(v,input){if(input?.tagName==='SELECT')return Array.from(input.options).find(o=>o.value===String(v))?.textContent||String(v);if(typeof v==='boolean')return v?'Ein':'Aus';return v===''?'Nicht eingetragen':typeof v==='number'?String(Number(v.toFixed(4))):String(v);}
 function value(row){return row.secret?row.input.value:row.boolean?row.input.checked:row.booleanSelect?row.input.value==='true':row.numeric?Number(row.input.value)*row.scale:row.input.value;}
 function changed(row){return row.secret?!!row.input.value:!same(value(row),row.saved);}
 function renew(){clearTimeout(timer);timer=setTimeout(()=>lock(),15*60*1000);}
 function lock(){window.teenetPhaseTest?.close();phaseAccess=false;cancelPhaseMqtt();cancelPhaseAccess();clearTimeout(timer);pin='';snapshot=null;rows=[];gpioRows=[];el('expert-content').hidden=true;el('expert-lock').hidden=false;el('expert-parameters').replaceChildren();el('expert-gpio-roles')?.replaceChildren();el('expert-pin').value='';update();message('Expertenbereich gesperrt.');}
 function chargingActive(){return !!(state?.enabled||state?.evcc_enabled||(state?.evcc_test_enabled&&!state.evcc_local_stop&&!state.evcc_lease_expired)||state?.actual_a?.some(a=>typeof a==='number'&&Number.isFinite(a)&&a>=1));}
 async function stopExpertCharging(){if(!pin||working||busy||!online)return;await action(async()=>{rememberChargeMode(el('mode').value);await api('/api/control',{enabled:false,mode:'off'});await pollStatus();message('Stopp angefordert. Parameter werden nach bestätigtem Stillstand freigegeben.');});}
 function editAllowed(){return !state?.phase_test_active&&!state?.firmware_check_pending&&online&&initialized&&!busy&&!uploading&&!configSaving&&state?.enabled===false&&!chargingActive()&&!state.phase_switching&&(!state.phase_switch_enabled||state.phase_state==='bereit')&&state.meter_ok===true&&Array.isArray(state.actual_a)&&state.actual_a.length===3&&state.actual_a.every(a=>typeof a==='number'&&Number.isFinite(a)&&a>=0&&a<1);}
 function curveModel(phases=3){
  const setting=key=>{const row=rows.find(r=>r.key===key),n=row?value(row):NaN;return Number.isFinite(n)&&n>=row?.min&&n<=row?.max?n:row?.standard??1;};
  const cfg=snapshot?.settings||{},minimum=setting('MIN_CURRENT');let maximum=Math.max(minimum,Math.min(63,Number(cfg.max_charge_a)||16));
  const carLimit=Number(cfg.pv_car_priority_w)/(230*phases),carPriority=cfg.pv_allocation_enabled&&cfg.battery_protect&&cfg.zero_feed_enabled&&Number(cfg.pv_priority)===1&&Number.isFinite(carLimit)&&carLimit>=minimum-.01;
  if(carPriority)maximum=Math.max(minimum,Math.min(maximum,carLimit));
  const startWait=setting('PV_START'),up=setting('PV_UP'),down=setting('PV_DOWN'),step=setting('PV_STEP'),lead=setting('PV_LEAD');
  const start=Math.min(maximum,minimum+setting('PV_START_MARGIN')),increment=Math.min(step,lead),deadband=setting('PV_DEADBAND');
  const points=[[0,0],[startWait,0],[startWait,start]];
  let peak=start,steps=0;while(steps<1000){const desired=Math.min(maximum,peak+lead);if(desired-peak<deadband-1e-6)break;const next=Math.min(desired,peak+step);if(next<=peak)break;steps++;const t=startWait+steps*up;points.push([t,peak],[t,next]);peak=next;}
  const topAt=startWait+steps*up,holdInterval=peak<maximum-1e-6?up:down,lastHold=topAt+Math.floor((15-1e-6)/holdInterval)*holdInterval;
  const reduceAt=Math.max(topAt+15,lastHold+down),end=reduceAt+15,low=peak-minimum>=deadband-1e-6?minimum:peak;
  points.push([reduceAt,peak],[reduceAt,low],[end,low]);
  return {minimum,maximum,start,startWait,up,down,step,lead,increment,topAt,reduceAt,end,peak,carPriority,points};
 }
 function drawCurve(){
  const chart=el('expert-shell-curve');if(!chart)return;
  const m=curveModel(),num=n=>Number(n.toFixed(2)).toLocaleString('de-DE'),clock=t=>`${Math.floor(t/60)}:${String(Math.round(t%60)).padStart(2,'0')}`;
  const plot=phases=>{
   const m=curveModel(phases),factor=.23*phases,limit=Math.ceil(m.maximum*factor/2)*2,x=t=>38+t/m.end*302,y=a=>172-a*factor/limit*138;
   const path=m.points.map(([t,a],i)=>`${i?'L':'M'}${x(t).toFixed(2)},${y(a).toFixed(2)}`).join(' ');
   const grid=[0,.5,1].map(v=>`<line x1="38" y1="${172-v*138}" x2="340" y2="${172-v*138}"/><text x="30" y="${176-v*138}" text-anchor="end">${num(limit*v)}</text>`).join('');
   const ticks=[0,.25,.5,.75,1].map(v=>`<text x="${38+v*302}" y="193" text-anchor="middle">${clock(v*m.end)}</text>`).join('');
   return `<section class="curve-phase curve-phase-${phases}"><div class="curve-phase-heading"><strong>${phases===1?'Einphasig':'Dreiphasig'}</strong><span>${num(m.minimum*factor)}–${num(m.maximum*factor)} kW</span></div><svg viewBox="0 0 370 216" role="img" aria-label="${phases===1?'Einphasige':'Dreiphasige'} PV-Regelkurve: Start, Hochregeln und Reduzieren"><g class="curve-grid">${grid}</g><rect class="curve-wait-area" x="38" y="34" width="${x(m.startWait)-38}" height="138"/><text class="curve-axis-label" x="38" y="18">kW</text><text class="curve-axis-label" x="340" y="212" text-anchor="end">Zeit · min:s</text><path class="curve-area" d="${path} L340,172 L38,172 Z"/><path class="curve-line" d="${path}"/>${ticks}<circle class="curve-dot" cx="${x(m.startWait)}" cy="${y(m.start)}" r="3.5"/><circle class="curve-dot" cx="${x(m.topAt)}" cy="${y(m.peak)}" r="3.5"/></svg><div class="curve-phase-details"><span>Start <b>${num(m.start*factor)} kW</b></span><span>Schritt <b>+${num(m.increment*factor)} kW</b></span><span>Rampe endet nach <b>${clock(m.topAt)}</b></span></div></section>`;
  };
  chart.innerHTML=`<div class="curve-heading"><div><h4>PV-Regelkurven</h4><p>So verändern die angezeigten Parameter die Stromvorgabe.</p></div><span class="curve-badge">Idealisiert</span></div><div class="curve-plots">${plot(1)}${plot(3)}</div><div class="curve-metrics"><div><small>Vor dem Start prüfen</small><b>${num(m.startWait)} s</b></div><div><small>Erhöhung je Schritt</small><b>${num(m.increment)} A / ${num(m.up)} s</b></div><div><small>Messvorsprung</small><b>${num(m.lead)} A</b></div><div><small>Herunterregeln nach</small><b>${num(m.down)} s</b></div></div><p class="curve-explanation">Beispiel: Überschuss prüfen → hochregeln → 15 s halten → auf Mindestleistung reduzieren. Berechnet mit 230 V und sofort folgender Fahrzeugmessung. Schrittweite, Messvorsprung und Toleranz begrenzen den Anstieg.</p><p class="curve-explanation">Keine Messkurve und kein Phasenwechsel. Gilt für PV-Überschuss; manuelle Vorgaben und evcc steuern direkt. Eine langsamere Fahrzeugreaktion verzögert den Anstieg. Wolkenpuffer und wechselnder Überschuss verändern den Verlauf.</p>`;
 }
 function updateGroupMarkers(){
  const panel=el('expert-settings');
  if(state!==lastMarkerState){lastMarkerState=state;if(typeof state?.expert_parameters_modified==='boolean')savedModified=state.expert_parameters_modified;if(snapshot&&typeof state?.phase_mqtt_allowed==='boolean')snapshot.phase_mqtt_allowed=state.phase_mqtt_allowed;}
  for(const group of [el('maintenance'),panel,...panel.querySelectorAll('.expert-section')]){
   const modified=(!pin||!rows.length)&&(group===panel||group===el('maintenance'))?savedModified:!!group.querySelector('.expert-row.is-modified');
   group.classList.toggle('has-modified-parameters',modified);
   const summary=group.querySelector(':scope > summary');
   if(summary){summary.classList.toggle('parameter-summary-modified',modified);if(modified)summary.setAttribute('title','Enthält Einstellungen, die vom Standard abweichen.');else summary.removeAttribute('title');}
  }
 }
 function update(){
  const editable=!!pin&&!working&&editAllowed();
  el('expert-edit-note').textContent=editAllowed()?'Änderungen gelten nach einem Neustart. Gelb markierte Zeilen weichen vom Standard ab.':state?.phase_test_active?'Während des Schütztests sind Regelparameter gesperrt.':'Eingaben sind bis zum bestätigten Ladestopp gesperrt.';
  el('expert-stop').hidden=!chargingActive();el('expert-stop').disabled=!online||!initialized||busy||working||uploading;
  for(const row of rows){row.input.disabled=!editable;row.reset.disabled=!editable;const differs=row.secret?!!snapshot?.settings[row.key+'_set']||!!row.input.value:!same(value(row),row.standard);row.node.classList.toggle('is-modified',differs);row.node.classList.toggle('is-unsaved',changed(row));}
  updatePhaseMqtt();updateGPIO();updateGroupMarkers();
  for(const [list,note,button] of [[rows,'expert-parameter-note','expert-save']]){
   const count=list.filter(changed).length;el(note).textContent=count?`${count} Änderung${count===1?'':'en'} noch nicht gespeichert.`:'Keine ungespeicherten Änderungen.';el(note).parentElement.classList.toggle('has-edits',!!count);el(button).disabled=!editable||!count;
   const normalNotice=el('functions-unsaved'),normalSettingsDirty=normalNotice&&!normalNotice.hidden&&normalNotice.dataset?.kind==='unsaved';
   el('expert-unsaved').hidden=!pin||!count||normalSettingsDirty;el('expert-unsaved-text').textContent=count===1?'Eine Änderung wartet auf das Speichern.':`${count} Änderungen warten auf das Speichern.`;el('expert-unsaved-save').disabled=!editable;
  }
  el('expert-unlock').disabled=working||!online||!initialized;
  el('expert-defaults').disabled=!editable;
  drawCurve();
 }
 let phaseMqttDraft=null,phaseMqttStage=0,phaseAccess=false,phaseBaseline=null;
 const phaseMessage=(text,error=false,normal=false)=>{const node=el(normal?'phase-setup-message':'expert-phase-output-message');node.textContent=text;node.classList.toggle('expert-message-error',error);};
 function phaseSettings(){return snapshot?.settings||configBaseline||{};}
 function fillPhaseSettings(data){
  const cfg=data.settings||data;
  el('expert-phase-mqtt-topic').value=cfg.phase_mqtt_topic||((cfg.mqtt_prefix||'wallbox-ems')+'/output/single_phase');
  el('expert-phase-mqtt-contactor').value=String(cfg.phase_mqtt_true_three_phase===true);
  el('expert-phase-output-transport').value=String(cfg.phase_output_transport||0);
  el('expert-phase-output-host').value=cfg.phase_output_host||'';
  el('expert-phase-output-channel').value=String(cfg.phase_output_switch_id||0);
  el('expert-phase-output-password').value='';
 }
 function openPhaseAccess(){
  if(working||busy||!online){phaseMessage('ESP nicht erreichbar oder beschäftigt.',true);return;}
  phaseAccess=false;phaseMessage('');el('phase-no-feedback-pin').value='';el('phase-no-feedback-unlock-error').textContent='';
  el('phase-no-feedback-unlock-dialog').showModal();updatePhaseMqtt();
 }
 function cancelPhaseAccess(){
  el('phase-no-feedback-pin').value='';if(el('phase-no-feedback-unlock-dialog').open)el('phase-no-feedback-unlock-dialog').close();
  
 }
 async function unlockPhaseAccess(){
  if(working||busy||!online)return;
  const candidate=el('phase-no-feedback-pin').value;
  if(!/^\d{4}$/.test(candidate)){el('phase-no-feedback-unlock-error').textContent='Passwort eingeben.';return;}
  working=true;updatePhaseMqtt();
  try{await api('/api/expert',{pin:candidate,action:'authorize'});pin=candidate;phaseAccess=true;
   fillPhaseSettings(snapshot||configBaseline||{});el('phase-no-feedback-unlock-dialog').close();phaseMessage('Ansteuerung einrichten, dann „Ohne Rückmeldung aktivieren“ wählen.');renew();
  }catch(error){el('phase-no-feedback-unlock-error').textContent=error.message;}
  finally{el('phase-no-feedback-pin').value='';working=false;updatePhaseMqtt();}
 }
 async function selectFeedback(){
  const choice=el('phase-feedback-choice'),f=el('config-form').elements;
  const count=choice.value==='2'?'2':'1',single=choice.value==='1-single';
  if(state?.phase_mqtt_allowed||snapshot?.phase_mqtt_allowed){
   working=true;updatePhaseMqtt();try{await writePhaseMqtt(false);}catch(error){phaseMessage(error.message,true,true);window.teenetShellyPhase?.update();return;}finally{working=false;}
  }
  f.phase_feedback_count.value=count;f.phase_feedback_closed_is_single.value=String(single);
  if(Number(f.phase_actuator_type.value)===2)f.phase_actuator_type.value='0';
  f.phase_feedback_enabled.checked=true;window.teenetShellyPhase?.update();
  f.phase_feedback_count.dispatchEvent(new Event('change',{bubbles:true}));phaseMessage('',false,true);updatePhaseMqtt();
 }
 function phaseOutputMethod(){return Number(el('expert-phase-output-transport').value||0);}
 function phaseOutputHostValid(){return /^[a-zA-Z0-9][a-zA-Z0-9.-]{0,62}$/.test(el('expert-phase-output-host').value.trim());}
 function phaseOutputPasswordReady(){return phaseOutputMethod()!==3||!!el('expert-phase-output-password').value||
  (phaseSettings().phase_output_password_set===true&&el('expert-phase-output-host').value.trim()===phaseSettings().phase_output_host);}
 function phaseOutputReady(){const method=phaseOutputMethod(),base=state?.phase_output_base_ready??snapshot?.phase_output_base_ready??snapshot?.phase_mqtt_ready;
  return base&&(state?.phase_output_meter_ready??snapshot?.phase_output_meter_ready)!==false&&phaseOutputPasswordReady()&&
   (method===0?(state?.phase_output_mqtt_ready??snapshot?.phase_output_mqtt_ready??snapshot?.phase_mqtt_ready):phaseOutputHostValid());}
 function phaseMqttStopped(){if(typeof state?.phase_output_setup_allowed==='boolean')return state.phase_output_setup_allowed;return state?.enabled===false&&!state?.evcc_enabled&&!state?.phase_switching&&state?.meter_ok===true&&
  Array.isArray(state.actual_a)&&state.actual_a.length===3&&state.actual_a.every(a=>Number.isFinite(a)&&a>=0&&a<1);}
 function updatePhaseMqtt(){
  const input=el('expert-phase-mqtt-enabled');if(!input)return;
  if(typeof configBaseline!=='undefined'&&configBaseline!==phaseBaseline){phaseBaseline=configBaseline;if(!phaseAccess)fillPhaseSettings(configBaseline||{});}
  if(state!==lastMarkerState&&snapshot&&typeof state?.phase_mqtt_allowed==='boolean')snapshot.phase_mqtt_allowed=state.phase_mqtt_allowed;
  const allowed=!!(snapshot?.phase_mqtt_allowed??state?.phase_mqtt_allowed);
  input.checked=allowed;
  input.disabled=working||!online||!initialized||busy||uploading||configSaving||
   (!allowed&&(!pin||!phaseAccess))||
   (!allowed&&(!phaseMqttStopped()||!phaseOutputReady()||state?.reboot_required));
  const edit=!!pin&&phaseAccess&&!working&&!busy&&online&&!chargingActive()&&!state?.phase_switching;
  el('phase-no-feedback-unlock-open').hidden=phaseAccess||allowed;
  el('expert-phase-output-edit-note').hidden=!allowed;
  el('phase-no-feedback-unlock-open').disabled=working||busy||!online;
  el('expert-phase-mqtt-topic').disabled=allowed||!edit;
  const type=el('expert-phase-mqtt-contactor');
  if(allowed)type.value=String(phaseSettings().phase_mqtt_true_three_phase===true);
  if(allowed){el('expert-phase-output-transport').value=String(phaseSettings().phase_output_transport||0);
   el('expert-phase-output-host').value=phaseSettings().phase_output_host||'';el('expert-phase-output-channel').value=String(phaseSettings().phase_output_switch_id||0);}
  for(const id of ['expert-phase-output-transport','expert-phase-output-host','expert-phase-output-channel','expert-phase-output-password'])el(id).disabled=allowed||!edit;
  const method=phaseOutputMethod();el('expert-phase-mqtt-topic').closest('label').hidden=method!==0;
  el('expert-phase-output-shelly').hidden=method===0;el('expert-phase-mqtt-broker-note').hidden=method!==0;
  el('expert-phase-output-password-label').hidden=method!==3;
  el('expert-phase-output-password').placeholder=phaseSettings().phase_output_password_set?'•••••••• · gespeichert':'Shelly-Gerätepasswort eingeben';
  el('expert-phase-output-summary').textContent=method===0?'TeeNet sendet Ein/Aus-Befehle an deinen Schaltaktor. Dieser betätigt das Schütz.':'Der Shelly betätigt die Schützspule. Das Schütz schaltet L2/L3.';
  el('expert-phase-output-shelly-note').textContent=(method===1?
   'Modbus TCP aktivieren. Der Shelly muss den gewählten Schaltausgang unterstützen.':
   method===3?'TeeNet meldet sich mit dem Gerätepasswort am Shelly an.':'Für diese Auswahl muss der Shelly ohne Passwort erreichbar sein.')+' Shelly-Timer und weitere Schaltwege deaktivieren.';
  type.disabled=allowed||!edit;
  el('expert-phase-mqtt').classList.toggle('is-modified',allowed);
  el('expert-phase-mqtt-status').textContent=allowed?'Aktiv · ohne Schützrückmeldung.':
   !online?'ESP nicht erreichbar.':!phaseAccess?'Warnung und Passwort bestätigen.':state?.reboot_required?'Zuerst neu starten.':
   !phaseMqttStopped()?'Ladung stoppen oder aktuelle Strommessung prüfen.':
   (state?.phase_output_meter_ready??snapshot?.phase_output_meter_ready)===false?
    (state?.wallbox_meter_type==='xemex'&&Number(state?.xemex_coils)<2?'Xemex: mindestens L1 und L2 messen.':'Mindestens L1 und L2 messen.'):
   phaseOutputMethod()>0&&!phaseOutputHostValid()?'Shelly-Adresse eingeben.':
   !phaseOutputPasswordReady()?'Shelly-Gerätepasswort eingeben.':
   method===0&&!(state?.phase_output_mqtt_ready??snapshot?.phase_output_mqtt_ready??snapshot?.phase_mqtt_ready)?'MQTT-Verbindung fehlt.':
   phaseOutputReady()?((state?.phase_output_measured_phases??snapshot?.phase_output_measured_phases)===2?'Freigabe möglich · L3 wird geschätzt.':'Deaktiviert · Freigabe möglich.'):'Messung oder Verbindung fehlt.';
 }
 function cancelPhaseMqtt(){phaseMqttStage=0;phaseMqttDraft=null;el('phase-mqtt-repeat-pin').value='';el('expert-phase-output-password').value='';if(el('phase-mqtt-confirm-dialog').open)el('phase-mqtt-confirm-dialog').close();update();}
 async function writePhaseMqtt(enabled,repeatPin=''){
  const request={pin,action:'mqtt_phase_output',enabled,topic:enabled?phaseMqttDraft.topic:'',true_three_phase:enabled?phaseMqttDraft.true_three_phase:false,
   transport:enabled?phaseMqttDraft.transport:0,host:enabled?phaseMqttDraft.host:'',switch_id:enabled?phaseMqttDraft.switch_id:0,repeat_pin:repeatPin,confirm_risk:enabled,confirm_no_feedback:enabled};
  if(enabled&&phaseMqttDraft.transport===3)request.password=phaseMqttDraft.password;
  const result=await api('/api/expert',request);
  await loadConfig();
  await pollStatus();
  if(pin&&snapshot){const previous=phaseSettings(),updated={...(snapshot.settings||{}),...(configBaseline||{})};
   if(enabled)Object.assign(updated,{phase_mqtt_topic:request.topic,phase_mqtt_true_three_phase:request.true_three_phase,
    phase_output_transport:request.transport,phase_output_host:request.host,phase_output_switch_id:request.switch_id,
    phase_output_password_set:request.transport===3?(!!request.password||previous.phase_output_password_set):false,phase_mqtt_output_enabled:true});
   else updated.phase_mqtt_output_enabled=false;
   snapshot.settings=updated;snapshot.phase_mqtt_allowed=typeof state?.phase_mqtt_allowed==='boolean'?state.phase_mqtt_allowed:enabled;fillPhaseSettings(snapshot);}
  else{snapshot=null;fillPhaseSettings(configBaseline);}
  updateSaveStatus(result);updateUnsavedFunctions();if(pin)renew();
  return result;
 }
 async function changePhaseMqtt(){
  const enabled=el('expert-phase-mqtt-enabled').checked;if(working)return;
  if(!enabled){working=true;updatePhaseMqtt();try{await writePhaseMqtt(false);phaseMessage('Deaktiviert. Es werden keine neuen Schaltbefehle gesendet.');}catch(error){phaseMessage(error.message,true);}finally{working=false;updatePhaseMqtt();}return;}
  if(!pin||!phaseAccess){updatePhaseMqtt();return;}
  const type=el('expert-phase-mqtt-contactor').value;
  const method=phaseOutputMethod();
  if(!phaseMqttStopped()||!['false','true'].includes(type)||![0,1,2,3].includes(method)||!phaseOutputReady()){phaseMessage('Ladung stoppen und Schütztyp sowie Verbindung prüfen.',true);update();return;}
  phaseMqttDraft={topic:el('expert-phase-mqtt-topic').value.trim()||((snapshot?.settings?.mqtt_prefix||'wallbox-ems')+'/output/single_phase'),true_three_phase:type==='true',
   transport:method,host:el('expert-phase-output-host').value.trim(),switch_id:Number(el('expert-phase-output-channel').value||0),password:method===3?el('expert-phase-output-password').value:''};
  el('phase-mqtt-confirm-topic').textContent=method===0?`Schalttopic: ${phaseMqttDraft.topic}`:
   `${['MQTT','Shelly Modbus TCP','Shelly HTTP ohne Passwort','Shelly HTTP mit Passwort'][method]}: ${phaseMqttDraft.host} · Ausgang ${phaseMqttDraft.switch_id}`;
  el('phase-mqtt-access-warning').textContent=method===0?'Schalttopic am Broker nur für TeeNet zum Schreiben freigeben. Andere Teilnehmer mit Schreibrechten könnten sonst das Schütz steuern.':
   'Shelly-Zugriff im Heimnetz auf TeeNet beschränken. Andere Teilnehmer könnten sonst das Schütz steuern. Shelly-Timer, weitere Schaltwege und Automatiken deaktivieren.';
  el('phase-mqtt-repeat-pin').value='';el('phase-mqtt-confirm-error').textContent='';phaseMqttStage=1;renderPhaseMqttStep();el('phase-mqtt-confirm-dialog').showModal();
 }
 function renderPhaseMqttStep(){
  el('phase-mqtt-confirm-title').textContent=phaseMqttStage===3?'Freigabe bestätigen':`Warnung ${phaseMqttStage} von 2`;
  el('phase-mqtt-warning-one').hidden=phaseMqttStage!==1;el('phase-mqtt-warning-two').hidden=phaseMqttStage!==2;
  el('phase-mqtt-password-step').hidden=phaseMqttStage!==3;el('phase-mqtt-confirm-next').hidden=phaseMqttStage===3;
  el('phase-mqtt-confirm-activate').hidden=phaseMqttStage!==3;
 }
 function nextPhaseMqttStep(){if(!pin||!phaseAccess||working||phaseMqttDraft===null||phaseMqttStage<1||phaseMqttStage>2)return;phaseMqttStage++;renderPhaseMqttStep();}
 async function activatePhaseMqtt(){
  if(!pin||!phaseAccess||working||phaseMqttDraft===null||phaseMqttStage!==3)return;
  const repeat=el('phase-mqtt-repeat-pin').value;
  if(!/^\d{4}$/.test(repeat)){el('phase-mqtt-confirm-error').textContent='Experten-Passwort erneut eingeben.';return;}
  await run(async()=>{try{const result=await writePhaseMqtt(true,repeat);cancelPhaseMqtt();
   phaseMessage(result.restart_needed?'Gespeichert. Neue Schnittstelle nach Neustart aktiv.':'Schützsteuerung freigegeben.');
  }catch(error){el('phase-mqtt-confirm-error').textContent=error.message;throw error;}finally{el('phase-mqtt-repeat-pin').value='';}});
 }
 function makeRow(meta,input,extra={}){
  const node=document.createElement('div');node.className='expert-row';
  const label=document.createElement('label'),title=document.createElement('span'),note=document.createElement('small'),standard=document.createElement('small');
  const id='expert-field-'+meta.key;input.id=id;input.setAttribute('aria-label',meta.label);input.removeAttribute('name');input.removeAttribute('data-secret');input.removeAttribute('data-write');input.removeAttribute('disabled');label.htmlFor=id;
  title.textContent=meta.label;note.textContent=meta.help;standard.className='expert-standard';standard.textContent=`Standard: ${readable(meta.standard,input)}${meta.unit?' '+meta.unit:''}`;
  label.append(title,note,standard);const reset=document.createElement('button');reset.type='button';reset.className='button expert-reset';reset.textContent='Standard';reset.setAttribute('aria-label',meta.label+' auf Standard setzen');
  const row={...meta,...extra,input,node,reset};reset.addEventListener('click',()=>{if(!(row.gpio?gpioEditable():editAllowed())||working)return;if(row.boolean)input.checked=row.standard;else input.value=row.secret?'':row.numeric?String(Number((row.standard/row.scale).toFixed(4))):String(row.standard);input.dispatchEvent(new Event('input',{bubbles:true}));});
  input.addEventListener('input',()=>{renew();update();});input.addEventListener('change',()=>{renew();update();});input.addEventListener('keydown',event=>{if(event.key==='Enter'&&!row.gpio){event.preventDefault();save();}});node.append(label,input,reset);return row;
 }
 function updateExpertCount(){el('expert-count').textContent=`${rows.length} Regelparameter${gpioRows.length?` · ${gpioRows.length} GPIO-Zuordnungen`:''}`;}
 function render(data){
  const preservedGPIO=snapshot?.gpio;snapshot={...data,gpio:data.gpio||preservedGPIO};lastMarkerState=state;rows=[];el('expert-parameters').replaceChildren();
  if(!phaseAccess)fillPhaseSettings(data);
  savedModified=typeof state?.expert_parameters_modified==='boolean'?state.expert_parameters_modified:data.parameters.some(meta=>!same(meta.value,meta.default))||snapshot.gpio?.roles.some(meta=>!same(meta.value,meta.default));
  const groups=new Map();
  for(const raw of data.parameters){const meta=parameterMeta(raw);let group=groups.get(meta.group);if(!group){group=document.createElement('details');group.className='expert-section';const summary=document.createElement('summary');summary.textContent=meta.group;group.append(summary);groups.set(meta.group,group);el('expert-parameters').append(group);}const input=document.createElement('input');input.type='number';input.min=meta.min;input.max=meta.max;input.step=meta.step;input.value=meta.value;
   const row=makeRow({key:meta.id,label:meta.label,help:meta.help,unit:meta.unit,standard:meta.default,saved:meta.value,min:meta.min,max:meta.max,numeric:true,scale:1},input);rows.push(row);group.append(row.node);
   if(!same(meta.active,meta.value)){const note=document.createElement('small');note.textContent=`Bis zum Neustart aktiv: ${meta.active} ${meta.unit}`;row.node.querySelector('label').append(note);}
  }
  const shellGroup=groups.get('Regelverhalten zur Shell');if(shellGroup){const chart=document.createElement('div');chart.id='expert-shell-curve';chart.className='expert-shell-curve';shellGroup.querySelector('summary').after(chart);}
  renderGPIO(snapshot);updateExpertCount();el('expert-content').hidden=false;el('expert-lock').hidden=true;renew();update();
 }
 async function run(work){if(working)return;working=true;update();try{await work();}catch(error){message(error.message,true);}finally{working=false;update();}}
 el('expert-unlock').addEventListener('click',()=>run(async()=>{const candidate=el('expert-pin').value;const data=await loadExpertParameters(candidate);pin=candidate;el('expert-pin').value='';render(data);message('Expertenbereich entsperrt.');}));
 el('expert-pin').addEventListener('keydown',e=>{if(e.key==='Enter'){e.preventDefault();el('expert-unlock').click();}});
 el('expert-lock-button').addEventListener('click',()=>{if([...rows,...gpioRows].some(changed)&&!confirm('Ungespeicherte Expertenänderungen verwerfen?'))return;lock();});
 el('expert-defaults').addEventListener('click',()=>{if(!editAllowed()||working||!confirm('Alle Regelparameter auf Standard setzen? Erst Speichern übernimmt die Änderungen.'))return;for(const row of rows)row.input.value=row.standard;update();renew();});
 async function save(){if(!pin)return;if(!editAllowed()){update();message('Parameter gesperrt. Ladung zuerst stoppen.',true);return;}updateUnsavedFunctions();if(el('functions-unsaved').dataset.kind==='unsaved'&&!el('functions-unsaved').hidden){message('Bitte zuerst die Änderungen in den normalen Einstellungskacheln speichern.',true);return;}const updates={};for(const row of rows){if(!row.input.checkValidity()){row.input.reportValidity();return;}if(changed(row))updates[row.key]=value(row);}if(!Object.keys(updates).length)return;
  await run(async()=>{const result=await api('/api/expert',{pin,action:'save',values:updates});const data=await loadExpertParameters(pin);render(data);updateSaveStatus(result);updateUnsavedFunctions();message(result.restart_needed?'Gespeichert. Nach einem Neustart wirksam.':'Einstellungen übernommen.');});
 }
 el('expert-save').addEventListener('click',save);
 el('expert-unsaved-save').addEventListener('click',save);
 el('expert-stop').addEventListener('click',stopExpertCharging);
 el('phase-feedback-choice').addEventListener('change',e=>{e.stopPropagation();selectFeedback();});
 el('phase-no-feedback-unlock-open').addEventListener('click',openPhaseAccess);
 el('phase-no-feedback-unlock-confirm').addEventListener('click',unlockPhaseAccess);
 el('phase-no-feedback-unlock-cancel').addEventListener('click',cancelPhaseAccess);
 el('phase-no-feedback-unlock-dialog').addEventListener('cancel',cancelPhaseAccess);
 el('phase-no-feedback-pin').addEventListener('keydown',e=>{if(e.key==='Enter'){e.preventDefault();unlockPhaseAccess();}});
 el('expert-phase-mqtt').addEventListener('input',e=>e.stopPropagation());el('expert-phase-mqtt').addEventListener('change',e=>e.stopPropagation());

 el('expert-phase-mqtt-enabled')?.addEventListener('change',changePhaseMqtt);
 el('expert-phase-mqtt-contactor').addEventListener('change',()=>{renew();updatePhaseMqtt();});
 for(const id of ['expert-phase-output-transport','expert-phase-output-host','expert-phase-output-channel','expert-phase-output-password'])el(id).addEventListener('input',()=>{renew();updatePhaseMqtt();});
 el('phase-mqtt-confirm-next').addEventListener('click',nextPhaseMqttStep);
 el('phase-mqtt-confirm-activate').addEventListener('click',activatePhaseMqtt);
 el('phase-mqtt-confirm-cancel').addEventListener('click',cancelPhaseMqtt);
 el('phase-mqtt-confirm-dialog').addEventListener('cancel',cancelPhaseMqtt);
 el('phase-mqtt-repeat-pin').addEventListener('keydown',e=>{if(e.key==='Enter'){e.preventDefault();activatePhaseMqtt();}});
 el('expert-settings').addEventListener('input',e=>e.stopPropagation());el('expert-settings').addEventListener('change',e=>e.stopPropagation());
/* Inside the password-protected expert UI. No pin is applied before reboot. */
let gpioRows=[];
function gpioEditable(){return !state?.phase_test_active&&!!pin&&!working&&online&&initialized&&!busy&&!uploading&&!configSaving&&!chargingActive()&&state?.gpio_setup_allowed===true;}
function gpioUsed(row){
 const c=configBaseline||snapshot?.settings||{},id=row.key;
 if(id.startsWith('wallbox_'))return Number(c.shell_rs485_interface||0)===0;
 if(id.startsWith('xemex_'))return Number(c.meter_rs485_interface||0)===0&&!['shelly_gen2','shelly_em1','em24_tcp'].includes(c.wallbox_meter_type);
 if(id.startsWith('house_'))return Number(c.house_rs485_interface||0)===0&&['xemex','sdm630','sdm630mct'].includes(c.house_meter_type);
 if(id==='external_mode_input_pin')return !!c.external_mode_input_enabled;
 if(id==='evu_input_pin')return !!c.evu_input_enabled;
 if(id==='relay1_pin')return !!c.relay_board_enabled&&Number(c.phase_actuator_type||0)===0;
 if(id.startsWith('phase_feedback'))return !!c.phase_switch_enabled&&Number(c.phase_actuator_type||0)===0&&(id==='phase_feedback_pin'||Number(c.phase_feedback_count)===2);
 return value(row)!==-1;
}
function renderGPIO(data){
 gpioRows=[];el('expert-gpio-roles')?.replaceChildren();
 if(!data.gpio)return;
 el('expert-gpio-board').textContent=data.gpio.board;
 for(const meta of data.gpio.roles){
  const input=document.createElement('select'),pins=data.gpio.pins.filter(p=>meta.led?p.led_allowed:!p.reason);
  if(meta.led)pins.unshift({pin:-1});
  for(const p of pins){const option=document.createElement('option');option.value=String(p.pin);option.textContent=p.pin===-1?'Aus':`GPIO ${p.pin}`;input.append(option);}
  input.value=String(meta.value);input.setAttribute('form','expert-gpio-form');
  const row=makeRow({key:meta.id,label:meta.label,help:meta.help,standard:meta.default,saved:meta.value,numeric:true,scale:1,gpio:true,led:meta.led},input);
  gpioRows.push(row);el('expert-gpio-roles').append(row.node);
 }
 updateGPIO();
}
function updateGPIO(){
 if(!gpioRows.length)return;
 const editable=gpioEditable(),occupied=new Map();
 for(const row of gpioRows)if(gpioUsed(row)&&value(row)!==-1){const pin=value(row);occupied.set(pin,[...(occupied.get(pin)||[]),row.label]);}
 let conflicts=0;
 for(const row of gpioRows){
  row.input.disabled=row.reset.disabled=!editable;
  const modified=!same(value(row),row.standard),invalid=gpioUsed(row)&&value(row)!==-1&&(occupied.get(value(row))||[]).length>1;
  conflicts+=Number(invalid);row.input.setCustomValidity(invalid?'GPIO ist mehrfach belegt. Eine andere freie Belegung wählen.':'');
  row.node.classList.toggle('is-modified',modified);row.node.classList.toggle('is-unsaved',changed(row));row.node.classList.toggle('gpio-conflict',invalid);
  for(const option of Array.from(row.input.options)){
   const pin=Number(option.value),others=(occupied.get(pin)||[]).filter(label=>label!==row.label);
   option.textContent=pin===-1?'Aus':`GPIO ${pin}${others.length?' · belegt: '+others.join(', '):pin===row.standard?' · Standard':''}`;
  }
 }
 const dirty=gpioRows.filter(changed).length;
 el('expert-gpio-note').textContent=conflicts?'Doppelte GPIO-Belegung aktiver Funktionen korrigieren.':dirty?`${dirty} GPIO-Änderung${dirty===1?'':'en'} noch nicht gespeichert.`:editable?'Keine ungespeicherten GPIO-Änderungen.':'GPIOs können nur bei gestoppter Ladung geändert werden.';
 el('expert-gpio-note').parentElement.classList.toggle('has-edits',!!dirty);
 el('expert-gpio-save').disabled=!editable||!dirty||!!conflicts;el('expert-gpio-defaults').disabled=!editable;
 const inventory=el('expert-gpio-inventory');inventory.replaceChildren();
 for(const p of snapshot.gpio.pins){
  const roles=gpioRows.filter(row=>value(row)===p.pin),line=document.createElement('div');line.className='gpio-pin-line';
  line.classList.toggle('is-modified',roles.some(row=>value(row)!==row.standard));
  const number=document.createElement('b'),usage=document.createElement('span');number.textContent=`GPIO ${p.pin}`;
  usage.textContent=roles.map(row=>`${row.label}${gpioUsed(row)?'':' · Funktion aus'}`).join(' / ')||p.reason||'Frei';
  line.append(number,usage);inventory.append(line);
 }
}
async function saveGPIO(){
 if(!gpioEditable()){message('GPIOs nur bei gestoppter Ladung ändern.',true);return;}
 updateUnsavedFunctions();if(el('functions-unsaved').dataset.kind==='unsaved'&&!el('functions-unsaved').hidden){message('Bitte zuerst Änderungen in den normalen Einstellungen speichern.',true);return;}
 const values={};for(const row of gpioRows){if(!row.input.checkValidity()){row.input.reportValidity();return;}if(changed(row))values[row.key]=value(row);}
 if(!Object.keys(values).length)return;
 await run(async()=>{const result=await api('/api/expert',{pin,action:'gpio',values});await loadConfig();await pollStatus();snapshot.gpio=await loadExpertGPIO(pin);renderGPIO(snapshot);updateExpertCount();updateSaveStatus(result);updateUnsavedFunctions();message('GPIO-Belegung gespeichert. Nach dem Neustart wirksam.');});
}
async function ensureExpertGPIO(){
 if(!el('expert-gpio-section').open||!pin||snapshot?.gpio||working)return;
 await run(async()=>{snapshot.gpio=await loadExpertGPIO(pin);renderGPIO(snapshot);updateExpertCount();message('GPIO-Belegung geladen.');});
}
el('expert-gpio-save')?.addEventListener('click',saveGPIO);
el('expert-gpio-defaults')?.addEventListener('click',()=>{if(!gpioEditable()||!confirm('Standardbelegung einsetzen? Erst Speichern übernimmt die Änderung.'))return;for(const row of gpioRows)row.input.value=String(row.standard);update();renew();});
el('expert-gpio-section')?.addEventListener('toggle',ensureExpertGPIO);


 window.teenetExpert={updateWrites:update};update();
})();
/* Only explicit local hardware tests; every movement asks for warning/PIN. */
(() => {
 let goal=0,requesting=false;
 function unsaved(){return !$('functions-unsaved').hidden&&$('functions-unsaved').dataset.kind==='unsaved';}
 function blocked(){
  if(!online)return 'ESP nicht erreichbar.';
  if(unsaved())return 'Zuerst die geänderten Einstellungen speichern.';
  if(state?.reboot_required)return 'Einstellungen gespeichert. Zuerst neu starten.';
  return state?.phase_test_ready?'':state?.phase_test_block||'Ladung stoppen und aktuelle Strommessung sowie Schützverbindung prüfen.';
 }
 function update(){
  const s=state,active=!!s?.phase_test_active;
  for(const id of ['phase-test-single','phase-test-three'])$(id).disabled=requesting||busy||!online||!initialized||active;
  $('phase-test-stop').hidden=!active;
  $('phase-test-stop').disabled=requesting||busy||!online;
  const reason=blocked();
  $('phase-test-status').textContent=active?`Test: ${s.phase_test_goal===1?'einphasig':'dreiphasig'} · Ladung bleibt gestoppt.`:reason||'Bereit · vor dem Schalten Warnung und Passwort bestätigen.';
  $('phase-test-block').hidden=!reason;
  $('phase-test-block').textContent=reason?`Test noch gesperrt: ${reason}`:'';
  $('phase-test-confirm').disabled=requesting||busy||!!reason||!goal;
 }
 function open(phases){
  if(state?.phase_test_active||busy||!online||!initialized||requesting)return;
  goal=phases;$('phase-test-pin').value='';$('phase-test-error').textContent='';
  $('phase-test-confirm-text').textContent=`${phases===1?'Einphasig':'Dreiphasig'} testen: Das angeschlossene Schütz wird dafür angesteuert.`;
  update();
  $('phase-test-dialog').showModal();
 }
 function close(){goal=0;$('phase-test-pin').value='';$('phase-test-dialog').close();}
 $('phase-test-single').addEventListener('click',()=>open(1));
 $('phase-test-three').addEventListener('click',()=>open(3));
 $('phase-test-cancel').addEventListener('click',close);
 $('phase-test-dialog').addEventListener('cancel',()=>{goal=0;$('phase-test-pin').value='';});
 $('phase-test-confirm').addEventListener('click',async()=>{
  if(requesting||!goal)return;
  if(blocked()){$('phase-test-error').textContent=blocked();return;}
  requesting=true;update();$('phase-test-confirm').disabled=true;
  try{await api('/api/phase-test',{phases:goal,pin:$('phase-test-pin').value,confirm_risk:true});close();await pollStatus();toast('Schütztest angefordert. Ladung bleibt gestoppt.');}
  catch(error){$('phase-test-error').textContent=error.message;}
  finally{$('phase-test-pin').value='';requesting=false;$('phase-test-confirm').disabled=false;update();}
 });
 $('phase-test-stop').addEventListener('click',()=>action(async()=>{await api('/api/phase-test',{stop:true});await pollStatus();toast('Schütztest beendet. Ladung bleibt gestoppt.');}));
 window.teenetPhaseTest={update,close};update();
})();





