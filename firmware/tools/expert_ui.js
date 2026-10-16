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
