'use strict';
/* Optional UI features share the normal command validation and never poll hardware. */
(() => {
  const planNames={off:'Kein Ladeplan aktiv.',pv:'PV hat Vorrang',grid:'Netz ergänzt das Ladeziel',complete:'Ladeziel erreicht',expired:'Endzeit erreicht · Ladeziel nicht vollständig erreicht',paused:'Pausiert · zum Fortsetzen bestätigen',clock:'Warte auf Internetzeit'};
  const eventNames={firmware_verified:'Firmware-Selbsttest bestanden',firmware_check_failed:'Firmware-Selbsttest fehlgeschlagen',gpio_saved:'GPIO-Belegung gespeichert',boot:'Controller gestartet',boot_watchdog:'Neustart nach Zeitüberschreitung',boot_brownout:'Neustart nach Spannungseinbruch',plan_started:'Ladeplan gestartet',plan_cancelled:'Ladeplan beendet',plan_grid:'Ladeplan ergänzt mit Netzstrom',plan_complete:'Ladeziel erreicht',plan_expired:'Endzeit des Ladeplans erreicht',control_changed:'Ladevorgabe geändert',user_stop:'Stopp angefordert',settings_saved:'Einstellungen gespeichert',settings_restored:'Einstellungen wiederhergestellt'};
  reasons.plan_clock='Ladeplan wartet auf eine gültige Internetzeit.';
  let eventsBusy=false,eventRows=[],persistedRows=[],planWasActive=false,huaweiSearching=false,huaweiChecking=false,huaweiDevices=[];
  async function huaweiScanStatus(){
    try{
      if(document.hidden)return;
      const result=await api('/api/huawei/scan');huaweiSearching=result.running;huaweiDevices=result.devices||[];
      $('huawei-scan-status').textContent=result.running?`Suche im Heimnetz · ${result.progress} %`:`${huaweiDevices.length} Huawei-Wechselrichter gefunden.${result.partial?' Suche teilweise abgeschlossen.':''}${huaweiDevices.length?'':' Modbus TCP am Smart Dongle aktivieren oder Adresse manuell eintragen.'}`;
      const select=$('huawei-found'),previous=select.value,empty=document.createElement('option');empty.value='';empty.textContent='Wechselrichter auswählen';
      select.replaceChildren(empty,...huaweiDevices.map((d,i)=>{const option=document.createElement('option');option.value=String(i);option.textContent=`${d.model} · ${d.host} · Adresse ${d.unit_id}`;return option;}));select.value=previous;select.hidden=!huaweiDevices.length;
    }catch(error){huaweiSearching=false;$('huawei-scan-status').textContent=error.message;}
    finally{writes();if(huaweiSearching)setTimeout(huaweiScanStatus,2500);}
  }
  $('huawei-scan').addEventListener('click',()=>action(async()=>{await api('/api/huawei/scan',{});huaweiSearching=true;$('huawei-scan-status').textContent='Suche gestartet …';setTimeout(huaweiScanStatus,300);}));
  $('huawei-found').addEventListener('change',()=>{if($('huawei-found').value==='')return;const d=huaweiDevices[Number($('huawei-found').value)];if(!d)return;const f=$('config-form').elements;f.huawei_host.value=d.host;f.huawei_unit_id.value=String(d.unit_id);$('huawei-preview-model').textContent=d.model;$('huawei-preview-state').textContent='Ausgewählt · Messwerte prüfen und anschließend speichern.';});
  async function huaweiPreviewStatus(){
    try{
      if(document.hidden)return;
      const r=await api('/api/huawei/preview');huaweiChecking=r.running;
      if(r.running){$('huawei-preview-state').textContent='Messwerte werden gelesen …';return;}
      const f=$('config-form').elements;if(r.host!==f.huawei_host.value.trim()||r.unit_id!==Number(f.huawei_unit_id.value))return;
      $('huawei-preview-model').textContent=r.model||'Keine Huawei-Verbindung';
      // Null means unsupported or missing, never a measured zero.
      $('huawei-preview-values').textContent=`Haus ${format(r.house_power_w==null?null:r.house_power_w/1000,2)} kW · PV ${format(r.pv_generation_w==null?null:r.pv_generation_w/1000,2)} kW · Akku ${format(r.battery_soc_pct,1)} %`;
      const phases=r.house_current_ok&&Array.isArray(r.house_a)&&r.house_a.length===3&&r.house_a.every(Number.isFinite);
      const phaseText=phases?r.house_a.map((a,i)=>`L${i+1} ${format(a,2)} A`).join(' · '):'Phasenwerte fehlen; bei aktiviertem Hausanschlussschutz gilt maximal 8 kW.';
      const missing=[r.house_power_w==null?'Hauszähler':null,r.pv_generation_w==null?'PV':null,r.battery_soc_pct==null?'Akku':null].filter(Boolean);
      const sampled=new Date().toLocaleTimeString('de-DE');
      $('huawei-preview-state').textContent=!r.connected?'Keine Verbindung. Modbus TCP, IP, Adresse und andere Modbus-Clients prüfen.':`Momentaufnahme ${sampled}. ${missing.length?'Keine gültigen Daten: '+missing.join(', ')+'.':'Alle Messwerte verfügbar.'} ${phaseText} Positiver Hauswert = Netzbezug.`;
    }catch(error){huaweiChecking=false;$('huawei-preview-state').textContent=error.message;}
    finally{writes();if(huaweiChecking)setTimeout(huaweiPreviewStatus,1000);}
  }
  $('huawei-test').addEventListener('click',()=>action(async()=>{const f=$('config-form').elements;await api('/api/huawei/preview',{host:f.huawei_host.value.trim(),unit_id:Number(f.huawei_unit_id.value)});huaweiChecking=true;setTimeout(huaweiPreviewStatus,300);}));
  let shellSearching=false;
  function useShell(device){const f=$('config-form').elements;f.shell_setup_host.value=device.host;f.shell_limits_auto.checked=true;f.grid_limit_a.value=device.grid_limit_a;f.max_charge_a.value=Math.min(16,device.max_charge_a);updateEquipment();toast('Shell-Grenzen übernommen. Einstellungen speichern.');}
  async function pollShellSearch(){
    try{const r=await api('/api/shell/scan');shellSearching=r.running;
      $('shell-search-status').textContent=r.running?`Netzwerksuche · ${r.progress} %`:r.devices.length?`${r.devices.length} Shell-Wallbox gefunden.`:'Keine Shell gefunden. Ohne LAN die beiden Grenzen von Hand eingeben.';
      $('shell-search-results').replaceChildren();for(const device of r.devices){const b=document.createElement('button');b.type='button';b.className='button';b.textContent=`${device.host} · ${device.max_charge_a} A Laden / ${device.grid_limit_a} A Netz`;b.addEventListener('click',()=>useShell(device));$('shell-search-results').append(b);}
      if(!r.running && r.devices.length===1)useShell(r.devices[0]);
    }catch(e){shellSearching=false;$('shell-search-status').textContent=e.message;}
    finally{if(shellSearching)setTimeout(pollShellSearch,1500);$('shell-search').disabled=shellSearching||busy||!online;}
  }
  $('shell-search').addEventListener('click',()=>action(async()=>{await api('/api/shell/scan',{});shellSearching=true;setTimeout(pollShellSearch,300);}));
  function localInput(date){const local=new Date(date.getTime()-date.getTimezoneOffset()*60000);return local.toISOString().slice(0,16);}
  const tomorrow=new Date();tomorrow.setDate(tomorrow.getDate()+1);tomorrow.setHours(7,0,0,0);
  $('plan-deadline').value=localInput(tomorrow);
  function render(){
    $('plan-panel').hidden=!!state?.basic_mode||!state?.charge_plan_enabled;
    const p=state?.charge_plan||{active:false,phase:'off',target_kwh:0,delivered_kwh:0};
    const when=p.deadline_epoch?new Date(p.deadline_epoch*1000).toLocaleString('de-DE',{day:'2-digit',month:'2-digit',hour:'2-digit',minute:'2-digit'}):'';
    $('plan-status').textContent=(p.phase==='grid'&&p.mode==='manual'?'Lädt bis zum Ladeziel':planNames[p.phase]||planNames.off)+(p.target_kwh>0?` · ${format(p.delivered_kwh,1)} / ${format(p.target_kwh,1)} kWh · bis ${when}`:'');
    $('plan-progress').hidden=!p.target_kwh;$('plan-progress').value=p.target_kwh?Math.min(100,p.delivered_kwh/p.target_kwh*100):0;
    $('plan-start').textContent=p.active?'Plan ersetzen':'Plan starten';
    $('plan-start').disabled=!online||busy||($('mode').value!=='manual'&&!state?.zero_feed_enabled)||!!state?.external_mode_input_enabled||!state?.clock_ok;
    $('plan-resume').hidden=!p.active||p.phase!=='paused';$('plan-cancel').hidden=!p.active;
    if(p.active&&!planWasActive)$('plan-panel').open=true;planWasActive=!!p.active;
    if(!p.active&&$('mode').value!=='manual'&&!state?.zero_feed_enabled)$('plan-status').textContent='Für den Ladeplan einen Hauszähler einrichten.';
    else if(state?.external_mode_input_enabled)$('plan-status').textContent='Ladeplan und Betriebsart-Kontakt sind nicht gleichzeitig nutzbar.';
    const f=$('config-form').elements;
    const auto=f.shell_limits_auto.checked,ready=auto&&state?.shell_limits_auto&&state?.shell_limits_ok;
    $('shell-manual-limits').hidden=!!ready;$('shell-address').hidden=!auto;
    $('shell-limit-summary').textContent=ready?`Aus Shell: ${format(state.max_charge_a,0)} A Laden · ${format(state.grid_limit_a,0)} A Netz`:
      auto?'Noch keine aktuellen Shell-Grenzen. Gespeicherte manuelle Werte gelten.':'Ohne LAN: 16 A Laden und 32 A Netz als Vorgabe. Werte mit der Shell-Einstellung abgleichen.';
    $('shell-search').disabled=shellSearching||busy||!online;
    updateWrites();
  }
  function updateWrites(){const networkBusy=huaweiSearching||huaweiChecking||meterScanning||previewRunning;$('plan-start').disabled=!online||busy||!state?.charge_plan_enabled||($('mode').value!=='manual'&&!state?.zero_feed_enabled)||!!state?.external_mode_input_enabled||!state?.clock_ok;window.teenetVehicles?.updateWrites();$('huawei-scan').disabled=networkBusy||!online||busy;$('huawei-test').disabled=networkBusy||!online||busy;}
  $('plan-start').addEventListener('click',()=>action(async()=>{
    if(window.teenetVehicles?.usesVehicle())return window.teenetVehicles.startPlan();
    const kwh=Number($('plan-kwh').value),deadline_epoch=Math.floor(new Date($('plan-deadline').value).getTime()/1000);
    if(!Number.isFinite(kwh)||kwh<.5||kwh>100||!Number.isFinite(deadline_epoch)||deadline_epoch<=Date.now()/1000||deadline_epoch>Date.now()/1000+7*86400)throw new Error('0,5 bis 100 kWh und eine Endzeit innerhalb der nächsten sieben Tage wählen.');
    if(state?.charge_plan?.active&&!window.confirm('Aktiven Ladeplan durch diesen Plan ersetzen?'))return;
    await api('/api/plan',{active:true,kwh,deadline_epoch,mode:$('mode').value==='manual'?'manual':'pv'});dirty=false;await pollStatus();toast('Ladeplan gestartet.');
  }));
  $('plan-resume').addEventListener('click',()=>action(async()=>{if(window.teenetVehicles?.isVehiclePlan())return window.teenetVehicles.resume();await api('/api/plan',{active:true,resume:true});dirty=false;await pollStatus();toast('Ladeplan fortgesetzt.');}));
  $('plan-cancel').addEventListener('click',()=>action(async()=>{await api('/api/plan',{active:false});dirty=false;await pollStatus();toast('Ladeplan beendet · Ladung gestoppt.');}));

  function download(value,name){const url=URL.createObjectURL(new Blob([JSON.stringify(value,null,2)],{type:'application/json'}));const link=document.createElement('a');link.href=url;link.download=name;document.body.append(link);link.click();link.remove();setTimeout(()=>URL.revokeObjectURL(url),5000);}
  $('backup-secrets').addEventListener('change',()=>{$('backup-warning').hidden=!$('backup-secrets').checked;});
  $('backup-download').addEventListener('click',()=>action(async()=>{const data=await api('/api/backup'+($('backup-secrets').checked?'?secrets=1':''),undefined,12000,true);download(data,'TeeNet-Einstellungen.json');toast('Einstellungen gesichert.');}));
  $('restore-upload').addEventListener('click',()=>action(async()=>{
    const file=$('restore-file').files[0];if(!file||file.size>24576)throw new Error('Eine TeeNet-Sicherung bis 24 KB auswählen.');
    let data;try{data=JSON.parse(await file.text());}catch(_){throw new Error('Die Datei enthält keine gültige Sicherung.');}
    if(data.format!=='TeeNet-settings'||![1,2].includes(data.schema)||!data.settings||typeof data.settings!=='object'||Array.isArray(data.settings))throw new Error('Eine passende TeeNet-Einstellungssicherung auswählen.');
    if(!window.confirm('Diese Einstellungen wiederherstellen? Die Ladung wird gestoppt. Anschließend neu starten und die Messwerte prüfen.'))return;
    await api('/api/restore',data);await loadConfig();await pollStatus();toast('Wiederhergestellt. Bitte neu starten und Messwerte prüfen.');
  }));
  function renderEvents(){
    const items=[...eventRows,...persistedRows.map(row=>({...row,persisted:true}))].map(row=>{
      const item=document.createElement('li'),title=document.createElement('strong'),time=document.createElement('time'),values=document.createElement('small');
      title.textContent=(row.persisted?'Neustart-Protokoll · ':'')+(eventNames[row.reason]||reasons[row.reason]||row.reason);
      time.textContent=row.epoch?new Date(row.epoch*1000).toLocaleString('de-DE',{day:'2-digit',month:'2-digit',hour:'2-digit',minute:'2-digit',second:'2-digit'}):`${Math.floor(row.uptime_s/60)} min nach Start`;
      values.textContent=`Soll ${format(row.target_kw,1)} kW · Wallbox ${format(row.actual_kw,1)} kW · Haus ${format(row.house_kw,1)} kW · Akku ${format(row.battery_pct,0)} %`;
      item.append(time,title,values);return item;
    });
    if(!items.length){const item=document.createElement('li');item.textContent='Noch keine Ereignisse vorhanden.';items.push(item);}
    $('events-list').replaceChildren(...items);
  }
  async function loadEvents(){if(eventsBusy||!online||document.hidden)return;eventsBusy=true;try{const data=await api('/api/events');eventRows=data.events||[];persistedRows=data.persisted_faults||[];renderEvents();}catch(error){toast(error.message,true);}finally{eventsBusy=false;}}
  $('events-refresh').addEventListener('click',loadEvents);
  $('events-export').addEventListener('click',async()=>{await loadEvents();download({format:'TeeNet-events',build_id:state?.build_id,events:eventRows,persisted_faults:persistedRows},'TeeNet-Ereignisse.json');});
  $('event-panel').addEventListener('toggle',()=>{if($('event-panel').open)loadEvents();});

  window.teennetFeatures={render,updateWrites,networkBusy:()=>huaweiSearching||huaweiChecking};
})();

/* Read-only heat-pump settings; simulation controls exist only in the demo. */
(() => {
 'use strict';
 const form=document.getElementById('config-form'),card=document.getElementById('heatpump-settings');
 if(!form||!card||form.dataset.heatpumpUiReady)return;
 form.dataset.heatpumpUiReady='true';
 const field=name=>form.elements.namedItem(name),el=id=>document.getElementById(id);
 let savedStatus=null;
 function refresh(){
  const basic=field('basic_mode').value==='true',enabled=!basic&&field('heatpump_enabled').checked;
  if(basic)field('heatpump_enabled').checked=false;
  card.hidden=!enabled;
  const mqtt=field('heatpump_source').value==='mqtt';
  el('heatpump-shelly-fields').hidden=mqtt;
  el('heatpump-mqtt-fields').hidden=!mqtt;
  field('heatpump_host').required=enabled&&!mqtt;
  field('heatpump_input_topic').required=enabled&&mqtt;
  const publish=field('heatpump_publish_enabled').checked;
  el('heatpump-output-field').hidden=!publish;
  field('heatpump_output_topic').required=enabled&&publish;
  if(el('heatpump-demo-apply'))el('heatpump-demo-apply').disabled=!savedStatus?.heatpump_enabled;
 }
 form.addEventListener('change',event=>{
  if(event.target===field('basic_mode')&&event.target.value==='true')field('heatpump_enabled').checked=false;
  if(event.target===field('heatpump_enabled')&&event.target.checked)card.open=true;
  if(['heatpump_source','heatpump_enabled'].includes(event.target.name)&&field('heatpump_enabled').checked&&field('heatpump_source').value==='mqtt')field('mqtt_enabled').checked=true;
  refresh();
 },true);
 window.addEventListener('teenet-status',event=>{
  savedStatus=event.detail;
  const live=el('heatpump-live-state');
  if(live)live.textContent=!savedStatus.heatpump_enabled?'Nach Aktivierung speichern und neu starten.':savedStatus.heatpump_ok?`Aktuell: ${Math.round(savedStatus.heatpump_w).toLocaleString('de-DE')} W`:'Kein aktueller Messwert. Quelle und Verbindung prüfen.';
  const status=el('heatpump-demo-result'),output=el('heatpump-demo-output');
  if(!status||!output){refresh();return;}
  status.textContent=!savedStatus.heatpump_enabled?'Funktion zuerst aktivieren und speichern.':
   savedStatus.heatpump_ok?`Simulierter Messwert: ${Math.round(savedStatus.heatpump_w).toLocaleString('de-DE')} W`:'Kein aktueller Messwert.';
  const published=Number.isFinite(savedStatus.heatpump_published_w);
  output.hidden=!savedStatus.heatpump_enabled||!field('heatpump_publish_enabled').checked;
  output.replaceChildren();
  if(!output.hidden){
   output.append('MQTT-Ausgang (simuliert): ');
   const topic=document.createElement('code');topic.textContent=savedStatus.heatpump_mqtt_topic||field('heatpump_output_topic').value;
   output.append(topic, published?` → ${Math.round(savedStatus.heatpump_published_w)} W`:' → kein gültiger Messwert oder MQTT deaktiviert');
  }
  refresh();
 });
 window.addEventListener('teenet:heatpump-config',()=>setTimeout(refresh,0));
 el('heatpump-demo-apply')?.addEventListener('click',async()=>{
  const value=el('heatpump-demo-watts'),button=el('heatpump-demo-apply'),note=el('heatpump-demo-result');
  if(!value.reportValidity())return;
  button.disabled=true;
  try{
   const response=await fetch('/api/demo/heatpump',{method:'POST',headers:{'Content-Type':'application/json'},body:JSON.stringify({power_w:Number(value.value),valid:!el('heatpump-demo-missing').checked})});
   const result=await response.json();
   if(!response.ok||result.ok===false)throw new Error(result.error||'Testwert nicht übernommen.');
   if(typeof pollStatus==='function')await pollStatus();
   else note.textContent='Testwert übernommen. Die Anzeige aktualisiert sich gleich.';
  }catch(error){note.textContent=error.message;}
  finally{refresh();}
 });
 el('heatpump-demo-watts')?.addEventListener('keydown',event=>{if(event.key==='Enter'){event.preventDefault();el('heatpump-demo-apply').click();}});
 refresh();
})();

/* VEHICLE_UI_BEGIN */
(() => {
 'use strict';
 const el=id=>document.getElementById(id);
 function estimate(p,soc,target,power){
  if(!p||![soc,target,p.capacity_kwh,p.efficiency,p.maximum_kw,power].every(Number.isFinite)||soc<0||target<=soc||target>100||p.capacity_kwh<5||p.efficiency<.5||p.efficiency>1||power<=0)return null;
  const kw=Math.min(power,p.maximum_kw),kwh=p.capacity_kwh*(target-soc)/100/p.efficiency,seconds=kwh/kw*3600;
  return {kwh,seconds,fullSeconds:p.capacity_kwh*(100-soc)/100/p.efficiency/kw*3600,margin:Math.max(300,seconds*.1)};
 }
 window.teenetVehicleEstimate=estimate;
 const form=el('vehicle-profile-form');if(!form)return;
 let data=null,loading=false,lastLoad=0,lastDataAt=0,dataFailed=false,editing=false,photo=null,photoRequest=0,photoBusy=false,chosen=false,lastRecognized=0,editorSlot=1,pendingEnabled=null,currentSocSlot=0,currentSocWasLive=false,previousPlanActive=false;
 const value=id=>el(id).value,num=id=>value(id).trim()===''?NaN:Number(value(id));
 const time=epoch=>new Date(epoch*1000).toLocaleTimeString('de-DE',{hour:'2-digit',minute:'2-digit'});
 const empty=()=>({name:'',plate:'',cards:['',''],capacity_kwh:0,maximum_kw:0,efficiency:0,target_soc:0,soc_source:0,picture:0});
 const validSoc=soc=>Number.isFinite(soc)&&soc>=0&&soc<=100;
 const profileName=(p,index)=>p.name||`Neues Fahrzeug ${index+1}`;
 const batteryVariants=[
  {picture:1,name:'ID.4 Pro · 77 kWh netto',capacity:77,source:'https://www.volkswagen.de/de/modelle/id4.html'},
  {picture:2,name:'Tiguan III eHybrid (2024) · 19,7 kWh netto',capacity:19.7,source:'https://www.volkswagen-newsroom.com/en/press-releases/now-available-to-order-long-range-plug-in-hybrid-drives-for-the-tiguan-from-48655-euro-18182'},
  {picture:3,name:'GLB 200 elektrisch (Stand 11/2026) · 58 kWh netto',capacity:58,source:'https://www.mercedes-benz.de/passengercars/models/suv/glb-electric/overview.html'},
  {picture:3,name:'GLB 250 elektrisch (Stand 11/2026) · 71 kWh netto',capacity:71,source:'https://www.mercedes-benz.de/passengercars/models/suv/glb-electric/overview.html'},
  {picture:3,name:'GLB 250+ / 350 4MATIC elektrisch (Stand 11/2026) · 85 kWh netto',capacity:85,source:'https://www.mercedes-benz.de/passengercars/models/suv/glb-electric/overview.html'},
  {picture:4,name:'CUPRA Born (07/2025) · 60 kWh netto',capacity:60,source:'https://www.cupraofficial.de/content/dam/countries/de/cupra-website/pdf/preisliste/Preisliste_CUPRA_Born_16-07-2025.pdf'},
  {picture:4,name:'CUPRA Born / VZ (07/2025) · 79 kWh netto',capacity:79,source:'https://www.cupraofficial.de/content/dam/countries/de/cupra-website/pdf/preisliste/Preisliste_CUPRA_Born_16-07-2025.pdf'},
  {picture:5,name:'Enyaq 60 (05/2025) · 59 kWh netto',capacity:59,source:'https://cdn.skoda-storyboard.com/2025/05/TD-Enyaq-en_0308de7f.pdf'},
  {picture:5,name:'Enyaq 85 / 85x (05/2025) · 77 kWh netto',capacity:77,source:'https://cdn.skoda-storyboard.com/2025/05/TD-Enyaq-en_0308de7f.pdf'},
  {picture:5,name:'Enyaq RS (05/2025) · 79 kWh netto',capacity:79,source:'https://cdn.skoda-storyboard.com/2025/05/TD-Enyaq-en_0308de7f.pdf'}
 ];
 function renameEditorOption(){const select=el('vehicle-edit-slot'),option=Array.from(select.options||select.children).find(o=>o.value===select.value);if(option)option.textContent=value('vehicle-name').trim()||`Neues Fahrzeug ${select.value}`;}
 function batteryOptions(){
  const select=el('vehicle-battery-variant');if(!select)return;
  const picture=num('vehicle-picture'),variants=batteryVariants.filter(v=>!picture||v.picture===picture),placeholder=document.createElement('option');placeholder.value='';placeholder.textContent='Eigene Angabe / andere Variante';
  select.replaceChildren(placeholder,...variants.map(v=>{const option=document.createElement('option');option.value=String(batteryVariants.indexOf(v));option.textContent=v.name;return option;}));
  const matched=variants.find(v=>v.capacity===num('vehicle-capacity'));select.value=matched?String(batteryVariants.indexOf(matched)):'';
  el('vehicle-battery-presets').hidden=false;const link=el('vehicle-battery-source');link.hidden=!matched;if(matched)link.href=matched.source;
 }
 el('vehicle-battery-variant')?.addEventListener('change',()=>{
  const key=value('vehicle-battery-variant'),variant=key===''?null:batteryVariants[Number(key)];if(!variant){el('vehicle-battery-source').hidden=true;return;}
  el('vehicle-capacity').value=String(variant.capacity);const link=el('vehicle-battery-source');link.href=variant.source;link.hidden=false;
  editing=true;el('vehicle-save-state').textContent='Akkugröße noch nicht gespeichert.';
 });
 function receive(next){
  if(!next||typeof next.enabled!=='boolean'||!Number.isInteger(next.revision)||!Number.isInteger(next.recognized_slot)||next.recognized_slot<0||next.recognized_slot>4||!Array.isArray(next.profiles)||next.profiles.length!==4||next.profiles.some(p=>!p||typeof p.name!=='string'||!Array.isArray(p.cards)||p.cards.length!==2||!Number.isFinite(p.capacity_kwh))||!next.plan||typeof next.plan.active!=='boolean'||typeof next.wallbox_fresh!=='boolean')throw Error('Fahrzeugdaten unvollständig. Erneut laden.');
  data=next;lastDataAt=Date.now();dataFailed=false;
 }
 function freshData(){const age=Date.now()-lastDataAt;return !!data&&online&&!dataFailed&&age>=0&&age<15000&&(data.fixed_profile>0||data.wallbox_fresh);}
 function fixedOptions(){
  const select=el('vehicle-fixed-profile');if(!select||!data)return;
  const selected=select.value||String(data.fixed_profile||0),auto=document.createElement('option');auto.value='0';auto.textContent='Automatisch per Ladekarte';
  select.replaceChildren(auto,...data.profiles.map((p,i)=>{const o=document.createElement('option');o.value=String(i+1);o.textContent=p.name?'Fest: '+p.name:`Noch kein Fahrzeug ${i+1}`;o.disabled=!p.name;return o;}));select.value=selected;
 }
 function featureDirty(){return pendingEnabled!==null&&pendingEnabled!==!!data?.enabled;}
 function syncFeature(){
  if(pendingEnabled===null)el('vehicle-enabled').checked=!!data?.enabled||!!el('config-form').elements.vehicle_soc_enabled?.checked;
  const basic=el('config-form').elements.basic_mode.value==='true';
  el('vehicle-profiles-settings').hidden=basic||!el('vehicle-enabled').checked;
  const soc=!!(el('config-form').elements.vehicle_soc_enabled?.checked??state?.vehicle_soc_enabled);
  if(el('vehicle-soc-settings'))el('vehicle-soc-settings').hidden=basic||!soc;
  if(el('vehicle-settings'))el('vehicle-settings').hidden=basic||(!soc&&!el('vehicle-enabled').checked);
 }
 async function saveFeature(){
  if(!featureDirty())return;
  if(!data)receive(await api('/api/vehicles',undefined,6000,true));
  const enabled=pendingEnabled;
  receive(await api('/api/vehicles',{enabled,revision:data.revision}));pendingEnabled=null;
  syncFeature();showPlans();
 }
 function notifyGraphic(){window.dispatchEvent(new CustomEvent('teenet-vehicle-display'));}
 function energyStatus(){
  const slot=freshData()&&data.enabled?data.recognized_slot:0,p=data?.profiles[slot-1];
  if(!p?.name)return {active:false,profilesEnabled:!!data?.enabled};
  const estimated=!!p.soc_estimated||!validSoc(p.soc)&&data.plan.active&&data.plan.slot===slot&&validSoc(data.plan.estimated_soc);
  const image=el('vehicle-active-photo');
  return {active:true,profilesEnabled:true,name:p.name,plate:p.plate,soc:validSoc(p.soc)?p.soc:estimated?data.plan.estimated_soc:null,estimated,
   image:image.dataset.loaded==='true'?image.src:'',target:data.plan.active&&data.plan.slot===slot?data.plan.target_soc:null};
 }
 function showPicture(id,slot,picture){
  const source=window.TeeNetVehiclePictures?.source(picture);
  if(picture&&source){const image=el(id);image.dataset.request='preset:'+picture+':'+Date.now();image.src=source;image.dataset.loaded='true';image.hidden=false;return;}
  loadImage(id,slot);
 }
 function paintPicture(){
  const selected=num('vehicle-picture');
  el('vehicle-picture-options').querySelectorAll('button').forEach(button=>button.setAttribute('aria-pressed',String(Number(button.dataset.picture)===selected)));
  batteryOptions();
 }
 for(const item of window.TeeNetVehiclePictures?.items||[]){
  if(!item.id)continue;
  const button=document.createElement('button'),image=document.createElement('img'),caption=document.createElement('span');
  button.type='button';button.className='vehicle-picture-option';button.dataset.picture=String(item.id);button.dataset.write='';button.setAttribute('form','vehicle-profile-form');button.setAttribute('aria-pressed','false');
  image.src=window.TeeNetVehiclePictures.source(item.id);image.alt=item.name;image.width=120;image.height=80;caption.textContent=item.name;button.append(image,caption);
  button.addEventListener('click',event=>{event.stopPropagation();photoRequest++;photoBusy=false;el('vehicle-picture').value=String(item.id);photo=null;showPicture('vehicle-photo-preview',num('vehicle-edit-slot'),item.id);paintPicture();editing=true;el('vehicle-save-state').textContent='Bildauswahl noch nicht gespeichert.';});
  el('vehicle-picture-options').append(button);
 }
 async function loadImage(id,slot){
  const image=el(id),request=String(slot)+':'+Date.now();image.dataset.request=request;image.hidden=true;image.dataset.loaded='false';
  try{const response=await fetch(`/api/vehicles/photo?slot=${slot}`);if(!response.ok)return;const blob=await response.blob();if(image.dataset.request!==request||blob.size>1536||blob.type!=='image/jpeg')return;if(image.dataset.objectUrl)URL.revokeObjectURL(image.dataset.objectUrl);image.dataset.objectUrl=URL.createObjectURL(blob);image.src=image.dataset.objectUrl;image.dataset.loaded='true';image.hidden=false;if(id==='vehicle-active-photo')notifyGraphic();}catch{}
 }
 function fillEditor(){
  photoRequest++;photoBusy=false;
  const slot=num('vehicle-edit-slot')||1,p=data?.profiles[slot-1]||empty();editorSlot=slot;
  syncFeature();
  for(const [id,key,fallback] of [['vehicle-name','name',''],['vehicle-plate','plate',''],['vehicle-capacity','capacity_kwh',''],['vehicle-max-kw','maximum_kw',11],['vehicle-efficiency','efficiency',.9],['vehicle-default-soc','target_soc',80],['vehicle-soc-source','soc_source',0]])el(id).value=p.name?p[key]:fallback;
  Array.from(el('vehicle-edit-slot').options||el('vehicle-edit-slot').children).forEach((option,i)=>option.textContent=profileName(data.profiles[i],i));
  el('vehicle-card-1').value=p.cards[0];el('vehicle-card-2').value=p.cards[1];
  el('vehicle-efficiency').value=p.name?Math.round(p.efficiency*100):90;
  el('vehicle-picture').value=String(p.picture||0);showPicture('vehicle-photo-preview',slot,p.picture||0);paintPicture();
  photo=null;el('vehicle-photo').value='';editing=false;el('vehicle-save-state').textContent='';showSource();
 }
 function showSource(){
  const p=data?.profiles[num('vehicle-edit-slot')-1],source=num('vehicle-soc-source');
  el('vehicle-soc-topic').textContent=source===1?(p?.soc_topic||''):source===2?'Vorhandene Fahrzeug-MQTT-Quelle; nur diesem Profil zuordnen.':'Start-Ladezustand vor jeder Ladung eingeben.';
 }
 function detailsData(){
  const p=freshData()&&data.enabled?data.profiles[data.recognized_slot-1]:null;if(!p?.name)return null;
  const rows=[['Kennzeichen',p.plate||'—'],['Akkugröße',p.capacity_kwh.toLocaleString('de-DE')+' kWh'],['Aktive Ladekarte',data.fixed_profile?'Fest ausgewähltes Fahrzeug':data.wallbox_fresh?data.detected_card||'—':'—'],['Profilauswahl',data.fixed_profile?'Fest ausgewählt':'Per Ladekarte'],['AC-Ladeleistung',p.maximum_kw.toLocaleString('de-DE')+' kW'],['Übliches Ladeziel',p.target_soc+' %'],['Zugeordnete Ladekarten',p.cards.filter(Boolean).join(' · ')||'—']];
  if(validSoc(p.soc))rows.unshift([p.soc_estimated?'Ladezustand (geschätzt)':'Ladezustand',(p.soc_estimated?'≈ ':'')+p.soc.toLocaleString('de-DE')+' %']);
  if(data.plan.active&&data.plan.slot===data.recognized_slot)rows.push(['Aktiver Ladeplan','Ziel '+data.plan.target_soc+' %']);
  return {name:p.name,image:energyStatus().image,rows};
 }
 function fillVehicleDetails(p){
  const details=detailsData();el('vehicle-details-name').textContent=p.name;
  el('vehicle-details-facts').replaceChildren(...(details?.rows||[]).map(([name,value])=>{const row=document.createElement('div'),term=document.createElement('dt'),definition=document.createElement('dd');term.textContent=name;definition.textContent=value;row.append(term,definition);return row;}));
 }
 function showDetails(){
  const p=freshData()&&data.enabled?data.profiles[data.recognized_slot-1]:null;if(!p?.name)return false;
  fillVehicleDetails(p);showPicture('vehicle-details-photo',data.recognized_slot,p.picture||0);
  const dialog=el('vehicle-details-dialog');if(!dialog.open)dialog.showModal();return true;
 }

 let startResolve=null,startSlot=0;
 function finishStart(value){const resolve=startResolve;startResolve=null;el('vehicle-start-soc-dialog').close();if(resolve)resolve(value);}
 async function beforeStart(){
  if(!el('vehicle-enabled').checked)return true;
  try{receive(await api('/api/vehicles',undefined,6000,true));}catch{toast('Fahrzeugdaten fehlen. Start ohne Ladestand-Schätzung.');return true;}showPlans();
  const p=data.enabled&&freshData()?data.profiles[data.recognized_slot-1]:null;
  if(data.manual_soc_prompt===false||!p?.name||(validSoc(p.soc)&&!p.soc_estimated))return true;
  startSlot=data.recognized_slot;el('vehicle-start-soc').value=validSoc(p.soc)?String(Math.round(p.soc)):'';el('vehicle-start-soc-error').textContent='';
  el('vehicle-start-soc-info').textContent=p.name+' · '+p.capacity_kwh.toLocaleString('de-DE')+' kWh Akku. Kein aktueller Ladestand verfügbar.';
  el('vehicle-start-soc-dialog').showModal();el('vehicle-start-soc').focus?.();
  return new Promise(resolve=>{startResolve=resolve;});
 }
 el('vehicle-start-soc-cancel').addEventListener('click',()=>finishStart(false));
 el('vehicle-start-soc-skip').addEventListener('click',()=>finishStart(true));
 el('vehicle-start-soc-dialog').addEventListener('cancel',event=>{event.preventDefault();finishStart(false);});
 el('vehicle-start-soc-form').addEventListener('submit',async event=>{
  event.preventDefault();const start_soc=num('vehicle-start-soc');
  if(!validSoc(start_soc)){el('vehicle-start-soc-error').textContent='Akkustand zwischen 0 und 100 % eingeben.';return;}
  el('vehicle-start-soc-accept').disabled=true;el('vehicle-start-soc-skip').disabled=true;
  try{receive(await api('/api/vehicles/estimate',{slot:startSlot,start_soc}));showPlans();finishStart(true);}
  catch(error){el('vehicle-start-soc-error').textContent=error.message;}
  finally{el('vehicle-start-soc-accept').disabled=false;el('vehicle-start-soc-skip').disabled=false;}
 });
 el('vehicle-details-open').addEventListener('click',showDetails);
 el('vehicle-details-close').addEventListener('click',()=>el('vehicle-details-dialog').close());
 function showPlans(){
  el('vehicle-plan-selector').hidden=false;el('plan-kind-vehicle').disabled=false;
  const view=data||{enabled:false,recognized_slot:0,profiles:[],plan:{active:false,maximum_kw:11},wallbox_fresh:false};
  const ready=freshData(),recognized=ready&&view.enabled?view.profiles[view.recognized_slot-1]:null;
  if(el('vehicle-details-dialog').open){if(recognized?.name)fillVehicleDetails(recognized);else el('vehicle-details-dialog').close();}
  el('vehicle-active-profile').hidden=!recognized?.name;
  const fixed=!!view.fixed_profile,card=recognized?.name&&ready&&view.wallbox_fresh&&!fixed?view.detected_card:'';
  el('vehicle-active-card').textContent=recognized?.name&&fixed?'Fest ausgewähltes Fahrzeug':card?`Aktive Ladekarte: ${card}`:'';
  el('vehicle-active-card').hidden=!recognized?.name||(!fixed&&!card);
  const soc=recognized?.soc,hasSoc=!!recognized?.name&&validSoc(soc);
  el('vehicle-active-soc').hidden=!hasSoc;
  el('vehicle-active-soc').setAttribute('aria-label',hasSoc?`Fahrzeugakku${recognized.soc_estimated?' (geschätzt)':''}: ${Math.round(soc)} %`:'Fahrzeugakku');
  el('vehicle-active-soc-value').textContent=hasSoc?`${recognized.soc_estimated?'≈ ':''}${Math.round(soc)} %`:'';
  el('vehicle-active-soc-fill').setAttribute('width',hasSoc?String(soc/2):'0');
  if(recognized?.name){el('vehicle-active-name').textContent=recognized.name;el('vehicle-active-info').textContent=[recognized.plate,`${recognized.capacity_kwh.toLocaleString('de-DE')} kWh Akku`].filter(Boolean).join(' · ');const key=view.recognized_slot+':'+data.revision;if(el('vehicle-active-photo').dataset.key!==key){el('vehicle-active-photo').dataset.key=key;showPicture('vehicle-active-photo',view.recognized_slot,recognized.picture||0);}}
  const planProfile=view.enabled?view.profiles[view.recognized_slot-1]:null,profiled=!!planProfile?.name;
  const direct=state?.charge_plan?.kind==='soc'?state.charge_plan:null;
  const globalSoc=online&&validSoc(state?.car_soc_pct)?state.car_soc_pct:null;
  const manualPower=value('mode')==='manual'&&Number.isFinite(state?.manual_power_kw)?state.manual_power_kw:Infinity;
  if(!profiled&&direct?.capacity_kwh&&value('plan-capacity').trim()==='')el('plan-capacity').value=direct.capacity_kwh;
  const p=profiled?{...planProfile,soc:ready?planProfile.soc:null}:{name:'Fahrzeug',capacity_kwh:num('plan-capacity'),efficiency:.9,maximum_kw:state?.max_charge_a*(state?.power_per_amp_kw||.69),soc:globalSoc??(direct?.active?direct.estimated_soc:null),soc_estimated:globalSoc===null,picture:0};
  if(!chosen&&!state?.charge_plan?.active&&validSoc(p.soc))el('plan-kind').value='vehicle';
  const mode=value('plan-kind')==='vehicle';el('vehicle-plan-fields').hidden=!mode;el('plan-kwh-label').hidden=mode;
  el('vehicle-plan-heading').hidden=!profiled;el('plan-capacity-label').hidden=profiled;
  const slot=profiled?view.recognized_slot:0;el('vehicle-plan-profile').value=slot?String(slot):'';el('vehicle-plan-profile').disabled=true;
  el('vehicle-plan-photo').hidden=!mode||!profiled||el('vehicle-plan-photo').dataset.loaded!=='true';
  if(mode&&profiled){const url=`/api/vehicles/photo?slot=${slot}&v=${view.revision}`;if(el('vehicle-plan-photo').dataset.url!==url){el('vehicle-plan-photo').dataset.url=url;showPicture('vehicle-plan-photo',slot,p.picture||0);}}
  const live=validSoc(p.soc)&&!p.soc_estimated,estimatedActive=!profiled&&direct?.active&&validSoc(direct.estimated_soc);el('vehicle-current-soc').disabled=live||estimatedActive;
  if(slot!==currentSocSlot||(!live&&currentSocWasLive)||(!live&&previousPlanActive&&!state?.charge_plan?.active))el('vehicle-current-soc').value='';
  if(live||estimatedActive||(p.soc_estimated&&validSoc(p.soc)&&value('vehicle-current-soc').trim()===''))el('vehicle-current-soc').value=Math.round(p.soc);
  currentSocSlot=slot;currentSocWasLive=live;previousPlanActive=!!state?.charge_plan?.active||view.plan.active;
  el('vehicle-soc-note').textContent=profiled&&!ready?'Keine aktuellen Fahrzeugdaten.':live?'Aktueller Fahrzeugwert per MQTT.':'Manueller Startwert · Fortschritt und Zeiten werden geschätzt.';
  const maximum=Math.min(view.plan.maximum_kw||11,manualPower),e=estimate(p,num('vehicle-current-soc'),num('vehicle-target-soc'),maximum),deadline=Math.floor(new Date(value('plan-deadline')).getTime()/1000),now=Date.now()/1000;
  el('vehicle-estimate').textContent=!mode?'':!e?'':`${e.kwh.toLocaleString('de-DE',{maximumFractionDigits:1})} kWh nachladen · Ziel frühestens ${time(now+e.seconds)}${value('mode')==='pv'&&Number.isFinite(deadline)?' · Netz spätestens '+time(deadline-e.seconds-e.margin):''}.${deadline<now+e.seconds?' Endzeit mit verfügbarer Leistung nicht erreichbar.':''}`;
  const plan=direct?.active?{...direct,maximum_kw:maximum,measured_soc:globalSoc!==null,deadline_feasible:true}:view.plan;
  if(plan.active){
   const names={wallbox_unavailable:'Pausiert: keine aktuellen Wallbox-Daten.',vehicle_card_missing:'Pausiert: Ladekarte nicht mehr bestätigt.',vehicle_not_ready:'Pausiert: Wallbox nicht ladebereit.',vehicle_changed:'Plan beendet: andere Karte oder neue Transaktion.',vehicle_disconnected:'Plan beendet: Fahrzeug getrennt.',profiles_disabled:'Plan beendet: Profile deaktiviert.'};
   el('vehicle-plan-live').textContent=(names[plan.reason]||`${view.profiles[plan.slot-1]?.name||'Fahrzeug'} · ${plan.measured_soc?'gemeldet':'geschätzt'} ${validSoc(plan.estimated_soc)?plan.estimated_soc.toLocaleString('de-DE',{maximumFractionDigits:0}):'—'} % → ${plan.target_soc} %`)+(plan.grid_start_epoch>0?` · Netz spätestens ${time(plan.grid_start_epoch)}`:'')+(!plan.deadline_feasible?' · Endzeit voraussichtlich nicht erreichbar.':'');
  }else el('vehicle-plan-live').textContent=plan.reason?({vehicle_changed:'Plan beendet: andere Karte oder neue Transaktion.',vehicle_disconnected:'Plan beendet: Fahrzeug getrennt.'}[plan.reason]||'Plan pausiert oder beendet.'):'';
  el('vehicle-plan-live').hidden=!el('vehicle-plan-live').textContent;notifyGraphic();
 }
 async function load(force=false){
  if(loading||!online||busy||document.hidden||(!force&&Date.now()-lastLoad<5000)||(!force&&data&&!data.enabled&&el('settings').hidden)||(!force&&el('settings').hidden&&el('overview').hidden))return;
  loading=true;lastLoad=Date.now();try{
   const next=await api('/api/vehicles',undefined,6000,true),first=!data,oldRevision=data?.revision;receive(next);
   const options=data.profiles.map((p,i)=>{const o=document.createElement('option');o.value=String(i+1);o.textContent=profileName(p,i);return o;});
   if(first||oldRevision!==data.revision){
    const selected=value('vehicle-plan-profile')||'',edit=value('vehicle-edit-slot')||'1';
    el('vehicle-edit-slot').replaceChildren(...options.map(o=>o.cloneNode(true)));el('vehicle-edit-slot').value=edit;
    fixedOptions();
    const none=document.createElement('option');none.value='';none.textContent='Fahrzeug fest auswählen oder Ladekarte vorhalten';el('vehicle-plan-profile').replaceChildren(none,...options.filter((_,i)=>!!data.profiles[i].name));el('vehicle-plan-profile').value=selected;
    if(!editing)fillEditor();
   }
   const recognized=data.enabled?data.profiles[data.recognized_slot-1]:null;
   if(data.recognized_slot&&data.recognized_slot!==lastRecognized)el('vehicle-plan-profile').value=String(data.recognized_slot);
   if(!chosen&&(first||data.recognized_slot!==lastRecognized)&&!state.charge_plan?.active)
    el('plan-kind').value=validSoc(recognized?.soc)||online&&validSoc(state?.car_soc_pct)?'vehicle':'kwh';
   if(state.charge_plan?.kind==='soc'&&state.charge_plan.active&&!chosen){el('plan-kind').value='vehicle';el('vehicle-target-soc').value=state.charge_plan.target_soc;el('plan-capacity').value=state.charge_plan.capacity_kwh;}
   if(data.plan.active&&!chosen){el('plan-kind').value='vehicle';el('vehicle-plan-profile').value=String(data.plan.slot);el('vehicle-target-soc').value=data.plan.target_soc;}
   else if(first||data.recognized_slot!==lastRecognized){const p=data.profiles[num('vehicle-plan-profile')-1];if(p?.name)el('vehicle-target-soc').value=p.target_soc;}
   lastRecognized=data.recognized_slot;
   el('vehicle-detected-card').textContent=data.fixed_profile?(data.profiles[data.fixed_profile-1]?.name?'Festes Profil: '+data.profiles[data.fixed_profile-1].name+' · keine Ladekarte nötig.':'Festes Fahrzeugprofil fehlt. Profil speichern oder Auswahl ändern.'):!data.wallbox_fresh?'Keine aktuellen Shell-Netzwerkdaten. Shell-IP unter Einstellungen einrichten.':data.detected_card?`Erkannte Karte: ${data.detected_card}${data.recognized_slot?' · '+data.profiles[data.recognized_slot-1].name:' · noch keinem Fahrzeug zugeordnet'}`:'Keine Ladekarte gemeldet.';
   el('vehicle-use-card').disabled=!data.detected_card;syncFeature();showPlans();if(typeof updateUnsavedFunctions==='function')updateUnsavedFunctions();
  }catch(e){dataFailed=true;el('vehicle-use-card').disabled=true;el('vehicle-detected-card').textContent='Keine aktuellen Fahrzeugdaten.';showPlans();if(force)toast(e.message,true);}finally{loading=false;}
 }
 async function save(remove=false){
  if(photoBusy&&!remove)throw Error('Bild wird noch vorbereitet. Kurz warten.');
  if(!data)throw Error('Fahrzeugprofile zuerst laden.');
  if(!remove&&!form.reportValidity())return;
  const slot=num('vehicle-edit-slot'),profile=remove?empty():{name:value('vehicle-name').trim(),plate:value('vehicle-plate').trim(),cards:[value('vehicle-card-1').trim(),value('vehicle-card-2').trim()],capacity_kwh:num('vehicle-capacity'),maximum_kw:num('vehicle-max-kw'),efficiency:num('vehicle-efficiency')/100,target_soc:num('vehicle-default-soc'),soc_source:num('vehicle-soc-source'),picture:num('vehicle-picture')};
  const next=await api('/api/vehicles',{slot,enabled:el('vehicle-enabled').checked,profile,revision:data.revision});receive(next);pendingEnabled=null;
  if(photo!==null||remove){try{await api(`/api/vehicles/photo?slot=${slot}`,{jpeg_base64:remove?'':photo});}catch(e){editing=false;fillEditor();throw Error('Profil gespeichert. '+e.message);}}
  editing=false;fillEditor();data=null;lastLoad=0;toast(remove?'Fahrzeugprofil gelöscht.':'Fahrzeugprofil gespeichert.');setTimeout(()=>load(true),0);
 }
 async function startPlan(resume=false){
  const configured=data?.enabled?data.profiles[data.recognized_slot-1]:null,profiled=!!configured?.name;
  const mode=resume?(state.charge_plan.mode||'pv'):value('mode')==='manual'?'manual':'pv';
  const deadline_epoch=resume?state.charge_plan.deadline_epoch:Math.floor(new Date(value('plan-deadline')).getTime()/1000);
  if(!Number.isFinite(deadline_epoch)||deadline_epoch<=Date.now()/1000||deadline_epoch>Date.now()/1000+7*86400)throw Error('Endzeit innerhalb der nächsten sieben Tage wählen.');
  const slot=data?.recognized_slot||0;
  if(profiled&&(!freshData()||!data.wallbox_fresh||(resume&&slot!==data.plan.slot)))throw Error('Aktuelle Fahrzeug- und Wallbox-Daten erforderlich.');
  const measured=profiled?validSoc(configured.soc)&&!configured.soc_estimated?configured.soc:null:online&&validSoc(state?.car_soc_pct)?state.car_soc_pct:null;
  const current_soc=measured??num('vehicle-current-soc'),target_soc=resume?data?.plan.target_soc:num('vehicle-target-soc');
  const p=profiled?configured:{capacity_kwh:num('plan-capacity'),efficiency:.9,maximum_kw:state.max_charge_a*(state.power_per_amp_kw||.69)};
  if(!estimate(p,current_soc,target_soc,Math.min(data?.plan.maximum_kw||11,mode==='manual'?state.manual_power_kw||11:Infinity)))throw Error('Akkugröße, Ladestand und höheres Ladeziel prüfen.');
  if(!resume&&state.charge_plan?.active&&!window.confirm('Aktiven Ladeplan ersetzen?'))return;
  if(profiled)receive(await api('/api/vehicles/plan',{slot,current_soc,target_soc,deadline_epoch,mode,...(resume?{resume:true}:{})}));
  else await api('/api/plan',resume?{active:true,resume:true,mode}:{active:true,current_soc,target_soc,capacity_kwh:p.capacity_kwh,deadline_epoch,mode});
  await pollStatus();showPlans();toast('Ladeplan '+(resume?'fortgesetzt.':'gestartet.'));
 }
 el('vehicle-profile-fields').addEventListener('input',event=>{event.stopPropagation();if(event.target.id==='vehicle-edit-slot')return;if(event.target.id==='vehicle-name')renameEditorOption();if(event.target.id==='vehicle-capacity')batteryOptions();editing=true;el('vehicle-save-state').textContent='Ungespeicherte Änderungen';showSource();});
 form.addEventListener('submit',event=>{event.preventDefault();action(()=>save());});
 el('vehicle-edit-slot').addEventListener('change',()=>{if(editing&&!window.confirm('Nicht gespeicherte Profiländerungen verwerfen?')){el('vehicle-edit-slot').value=String(editorSlot);return;}fillEditor();});
 el('vehicle-use-card').addEventListener('click',()=>{if(freshData()&&data.detected_card){el('vehicle-card-1').value=data.detected_card;editing=true;el('vehicle-save-state').textContent='Ungespeicherte Änderungen';}});
 el('vehicle-delete').addEventListener('click',()=>{if(window.confirm('Dieses Fahrzeugprofil und sein Bild löschen?'))action(()=>save(true));});
 el('vehicle-remove-photo').addEventListener('click',()=>{photoRequest++;photoBusy=false;photo='';el('vehicle-picture').value='0';paintPicture();editing=true;el('vehicle-photo-preview').dataset.request='removed:'+Date.now();el('vehicle-photo-preview').hidden=true;el('vehicle-save-state').textContent='Ungespeicherte Änderungen';});
 el('vehicle-photo').addEventListener('change',async()=>{
  const file=el('vehicle-photo').files[0];if(!file)return;
  const request=++photoRequest,slot=editorSlot;photoBusy=true;
  try{
   if(file.size>8*1024*1024||!['image/jpeg','image/png','image/webp'].includes(file.type))throw Error('JPEG-, PNG- oder WebP-Bild bis 8 MB wählen.');
   const image=await createImageBitmap(file);if(request!==photoRequest||slot!==editorSlot){image.close();return;}const canvas=document.createElement('canvas');let encoded='';
   for(const width of [120,96,72]){canvas.width=width;canvas.height=Math.round(width*2/3);const ctx=canvas.getContext('2d');ctx.fillStyle='#fff';ctx.fillRect(0,0,canvas.width,canvas.height);const scale=Math.min(canvas.width/image.width,canvas.height/image.height);ctx.drawImage(image,(canvas.width-image.width*scale)/2,(canvas.height-image.height*scale)/2,image.width*scale,image.height*scale);for(const quality of [.75,.5,.3,.15]){encoded=canvas.toDataURL('image/jpeg',quality).split(',')[1];if(encoded.length<=2048)break;}if(encoded.length<=2048)break;}
   image.close();if(encoded.length>2048)throw Error('Bild zu detailreich für die kleine Fahrzeugvorschau.');
   photo=encoded;el('vehicle-picture').value='0';paintPicture();editing=true;el('vehicle-photo-preview').dataset.request='upload:'+Date.now();el('vehicle-photo-preview').src='data:image/jpeg;base64,'+photo;el('vehicle-photo-preview').hidden=false;el('vehicle-save-state').textContent='Bild noch nicht gespeichert.';
  }catch(e){if(request===photoRequest)toast(e.message,true);}finally{if(request===photoRequest)photoBusy=false;}
 });
 for(const id of ['vehicle-photo-preview','vehicle-plan-photo','vehicle-active-photo'])el(id).addEventListener('error',()=>{el(id).hidden=true;el(id).dataset.loaded='false';});
 for(const id of ['plan-kind','vehicle-plan-profile','vehicle-current-soc','vehicle-target-soc','plan-capacity','plan-deadline'])el(id).addEventListener('input',()=>{chosen=true;if(id==='vehicle-plan-profile'){el('vehicle-current-soc').value='';const p=data?.profiles[num(id)-1];if(p?.name)el('vehicle-target-soc').value=p.target_soc;}showPlans();});
 el('plan-kind').addEventListener('change',()=>{chosen=true;showPlans();});
 el('vehicle-profiles-settings').addEventListener('toggle',()=>{if(el('vehicle-profiles-settings').open)load(true);});
 el('vehicle-enabled').addEventListener('change',event=>{event.stopPropagation();pendingEnabled=el('vehicle-enabled').checked;if(!pendingEnabled&&el('config-form').elements.vehicle_soc_enabled)el('config-form').elements.vehicle_soc_enabled.checked=false;syncFeature();if(pendingEnabled)el('vehicle-profiles-settings').open=true;updateUnsavedFunctions();});
 el('vehicle-profile-fields').addEventListener('change',event=>event.stopPropagation());
 window.addEventListener('teenet-status',()=>{syncFeature();showPlans();load();});
 window.teenetVehicles={syncFeature,isDirty:featureDirty,saveFeature,energyStatus,detailsData,showDetails,beforeStart,basicMode(){pendingEnabled=false;el('vehicle-enabled').checked=false;syncFeature();},updateWrites:()=>{if(data?.enabled&&data.profiles[data.recognized_slot-1]?.name&&value('plan-kind')==='vehicle'&&(!freshData()||!data.wallbox_fresh))el('plan-start').disabled=true;},usesVehicle:()=>value('plan-kind')==='vehicle',startPlan,resume:()=>data?.plan.active?startPlan(true):null,isVehiclePlan:()=>!!data?.plan.active};
})();
/* VEHICLE_UI_END */
