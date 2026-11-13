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
  if(dynamic.personal||Array.isArray(dynamic.parameters))return dynamic;
  const schema=await expertSchema(),values=dynamic.values,active=dynamic.active;
  if(!Array.isArray(schema.parameters)||!Array.isArray(values)||!Array.isArray(active)||values.length!==schema.parameters.length||active.length!==values.length)throw Error('Expertenparameter unvollständig.');
  return {parameters:schema.parameters.map((meta,index)=>({...meta,factory:meta.default,default:dynamic.standards?.[meta.id]??meta.default,value:values[index],active:active[index]})),settings:configBaseline||{},pending:!!dynamic.pending,
   phase_mqtt_allowed:state?.phase_mqtt_allowed,phase_mqtt_ready:state?.phase_mqtt_ready,phase_output_base_ready:state?.phase_output_base_ready,
   phase_output_meter_ready:state?.phase_output_meter_ready,phase_output_measured_phases:state?.phase_output_measured_phases,phase_output_mqtt_ready:state?.phase_output_mqtt_ready};
 }
 async function loadExpertGPIO(accessPin){return api('/api/expert',{pin:accessPin,action:'read_gpio'});}
 function readable(v,input){if(input?.tagName==='SELECT')return Array.from(input.options).find(o=>o.value===String(v))?.textContent||String(v);if(typeof v==='boolean')return v?'Ein':'Aus';return v===''?'Nicht eingetragen':typeof v==='number'?String(Number(v.toFixed(4))):String(v);}
 function value(row){return row.secret?row.input.value:row.boolean?row.input.checked:row.booleanSelect?row.input.value==='true':row.numeric?Number(row.input.value)*row.scale:row.input.value;}
 function changed(row){return row.secret?!!row.input.value:!same(value(row),row.saved);}
 function renew(){clearTimeout(timer);timer=setTimeout(()=>lock(),15*60*1000);}
 function lock(){window.teenetPersonal?.lock();window.teenetPhaseTest?.close();clearTimeout(timer);pin='';snapshot=null;rows=[];gpioRows=[];el('expert-content').hidden=true;el('expert-lock').hidden=false;el('expert-parameters').replaceChildren();el('expert-gpio-roles')?.replaceChildren();el('expert-pin').value='';update();message('Expertenbereich gesperrt.');}
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
   if(summary){summary.classList.toggle('parameter-summary-modified',modified);if(modified)summary.setAttribute('title','Enthält Einstellungen, die von der Vorgabe abweichen.');else summary.removeAttribute('title');}
  }
 }
 function update(){
  const editable=!!pin&&!working&&editAllowed();
  el('expert-edit-note').textContent=editAllowed()?'Änderungen gelten nach einem Neustart. Gelb markierte Zeilen weichen von der Vorgabe ab.':state?.phase_test_active?'Während des Schütztests sind Regelparameter gesperrt.':'Eingaben sind bis zum bestätigten Ladestopp gesperrt.';
  el('expert-stop').hidden=!chargingActive();el('expert-stop').disabled=!online||!initialized||busy||working||uploading;
  for(const row of rows){row.input.disabled=!editable;row.reset.disabled=!editable;if(row.adopt)row.adopt.disabled=!editable||changed(row)||!same(value(row),row.active)||same(value(row),row.standard);const differs=row.secret?!!snapshot?.settings[row.key+'_set']||!!row.input.value:!same(value(row),row.standard);row.node.classList.toggle('is-modified',differs);row.node.classList.toggle('is-unsaved',changed(row));}
  updateGPIO();updateGroupMarkers();
  for(const [list,note,button] of [[rows,'expert-parameter-note','expert-save']]){
   const count=list.filter(changed).length;el(note).textContent=count?`${count} Änderung${count===1?'':'en'} noch nicht gespeichert.`:'Keine ungespeicherten Änderungen.';el(note).parentElement.classList.toggle('has-edits',!!count);el(button).disabled=!editable||!count;
   const normalNotice=el('functions-unsaved'),normalSettingsDirty=normalNotice&&!normalNotice.hidden&&normalNotice.dataset?.kind==='unsaved';
   el('expert-unsaved').hidden=!pin||!count||normalSettingsDirty;el('expert-unsaved-text').textContent='Speichern und neu starten, dann die markierten Werte prüfen. Erst nach erfolgreichem Test als neuen Standard setzen.';el('expert-unsaved-save').disabled=!editable;
  }
  el('expert-unlock').disabled=working||!online||!initialized;
  el('expert-defaults').disabled=!editable;
  drawCurve();
 }
 function makeRow(meta,input,extra={}){
  const node=document.createElement('div');node.className='expert-row';
  const label=document.createElement('label'),title=document.createElement('span'),note=document.createElement('small'),standard=document.createElement('small');
  const id='expert-field-'+meta.key;input.id=id;input.setAttribute('aria-label',meta.label);input.removeAttribute('name');input.removeAttribute('data-secret');input.removeAttribute('data-write');input.removeAttribute('disabled');label.htmlFor=id;
  title.textContent=meta.label;note.textContent=meta.help;standard.className='expert-standard';standard.textContent=`Werk: ${readable(meta.factory??meta.standard,input)}${meta.unit?' '+meta.unit:''}`;
  label.append(title,note,standard);const reset=document.createElement('button');reset.type='button';reset.className='button expert-reset';reset.textContent='Standard';reset.setAttribute('aria-label',meta.label+' auf persönliche Vorgabe zurücksetzen');
  const adopt=document.createElement('button');adopt.type='button';adopt.className='button expert-adopt';adopt.textContent='Als neuen Standard setzen';adopt.setAttribute('aria-label',meta.label+' nach erfolgreicher Prüfung als neuen Standard übernehmen');
  adopt.title='Zuerst speichern, neu starten und die Werte prüfen.';const row={...meta,...extra,input,node,reset,adopt};adopt.addEventListener('click',()=>{if(!(row.gpio?gpioEditable():editAllowed())||!row.input.checkValidity()||changed(row)||!same(value(row),row.active))return;run(async()=>{const selected=value(row);await api('/api/expert',{pin,action:'standard',id:row.key,value:selected});row.standard=selected;if(row.gpio){const meta=snapshot?.gpio?.roles.find(meta=>meta.id===row.key);if(meta)meta.default=selected;}await pollStatus();message('Neuer Standard gespeichert.');renew();});});reset.addEventListener('click',()=>{if(!(row.gpio?gpioEditable():editAllowed())||working)return;if(row.boolean)input.checked=row.standard;else input.value=row.secret?'':row.numeric?String(Number((row.standard/row.scale).toFixed(4))):String(row.standard);input.dispatchEvent(new Event('input',{bubbles:true}));});
  input.addEventListener('input',()=>{renew();update();});input.addEventListener('change',()=>{renew();update();});input.addEventListener('keydown',event=>{if(event.key==='Enter'&&!row.gpio){event.preventDefault();save();}});node.append(label,input,reset,adopt);return row;
 }
 function updateExpertCount(){el('expert-count').textContent=`${rows.length} Regelparameter${gpioRows.length?` · ${gpioRows.length} GPIO-Zuordnungen`:''}`;}
 function render(data){
  const preservedGPIO=snapshot?.gpio;snapshot={...data,gpio:data.gpio||preservedGPIO};lastMarkerState=state;rows=[];el('expert-parameters').replaceChildren();
  savedModified=typeof state?.expert_parameters_modified==='boolean'?state.expert_parameters_modified:data.parameters.some(meta=>!same(meta.value,meta.default))||snapshot.gpio?.roles.some(meta=>(meta.used||meta.led)&&!same(meta.value,meta.default));
  const groups=new Map();
  for(const raw of data.parameters){const meta=parameterMeta(raw);let group=groups.get(meta.group);if(!group){group=document.createElement('details');group.className='expert-section';const summary=document.createElement('summary');summary.textContent=meta.group;group.append(summary);groups.set(meta.group,group);el('expert-parameters').append(group);}const input=document.createElement('input');input.type='number';input.min=meta.min;input.max=meta.max;input.step=meta.step;input.value=meta.value;
   const row=makeRow({key:meta.id,label:meta.label,help:meta.help,unit:meta.unit,standard:meta.default,factory:meta.factory??meta.default,saved:meta.value,active:meta.active,min:meta.min,max:meta.max,numeric:true,scale:1},input);rows.push(row);group.append(row.node);
   if(!same(meta.active,meta.value)){const note=document.createElement('small');note.textContent=`Bis zum Neustart aktiv: ${meta.active} ${meta.unit}`;row.node.querySelector('label').append(note);}
  }
  const shellGroup=groups.get('Regelverhalten zur Shell');if(shellGroup){const chart=document.createElement('div');chart.id='expert-shell-curve';chart.className='expert-shell-curve';shellGroup.querySelector('summary').after(chart);}
  renderGPIO(snapshot);updateExpertCount();el('expert-content').hidden=false;el('expert-lock').hidden=true;renew();update();
 }
 async function run(work){if(working)return;working=true;update();try{await work();}catch(error){message(error.message,true);}finally{working=false;update();}}
 el('expert-unlock').addEventListener('click',()=>run(async()=>{const candidate=el('expert-pin').value;const data=await loadExpertParameters(candidate);if(data.personal){window.teenetPersonal?.open(candidate,data);el('expert-pin').value='';return;}pin=candidate;el('expert-pin').value='';render(data);message('Expertenbereich entsperrt.');}));
 el('expert-pin').addEventListener('keydown',e=>{if(e.key==='Enter'){e.preventDefault();el('expert-unlock').click();}});
 el('expert-lock-button').addEventListener('click',()=>{if([...rows,...gpioRows].some(changed)&&!confirm('Ungespeicherte Expertenänderungen verwerfen?'))return;lock();});
 el('expert-defaults').addEventListener('click',()=>{if(!editAllowed()||working||!confirm('Alle Regelparameter auf die persönlichen Vorgaben zurücksetzen? Erst Speichern übernimmt die Änderungen.'))return;for(const row of rows)row.input.value=row.standard;update();renew();});
 async function save(){if(!pin)return;if(!editAllowed()){update();message('Parameter gesperrt. Ladung zuerst stoppen.',true);return;}updateUnsavedFunctions();if(el('functions-unsaved').dataset.kind==='unsaved'&&!el('functions-unsaved').hidden){message('Bitte zuerst die Änderungen in den normalen Einstellungskacheln speichern.',true);return;}const updates={};for(const row of rows){if(!row.input.checkValidity()){row.input.reportValidity();return;}if(changed(row))updates[row.key]=value(row);}if(!Object.keys(updates).length)return;
  await run(async()=>{const result=await api('/api/expert',{pin,action:'save',values:updates});const data=await loadExpertParameters(pin);render(data);updateSaveStatus(result);updateUnsavedFunctions();message(result.restart_needed?'Gespeichert. Nach einem Neustart wirksam.':'Einstellungen übernommen.');});
 }
 el('expert-save').addEventListener('click',save);
 el('expert-unsaved-save').addEventListener('click',save);
 el('expert-stop').addEventListener('click',stopExpertCharging);
 el('expert-settings').addEventListener('input',e=>e.stopPropagation());el('expert-settings').addEventListener('change',e=>e.stopPropagation());
/* Inside the password-protected expert UI. No pin is applied before reboot. */
let gpioRows=[];
function gpioEditable(){return !state?.phase_test_active&&!!pin&&!working&&online&&initialized&&!busy&&!uploading&&!configSaving&&!chargingActive()&&state?.gpio_setup_allowed===true;}
function gpioUsed(row){
 const c=configBaseline||snapshot?.settings||{},id=row.key;
 if(id.startsWith('wallbox_'))return Number(c.shell_rs485_interface||0)!==1;
 if(id.startsWith('xemex_'))return Number(c.meter_rs485_interface||0)!==1&&!['shelly_gen2','shelly_em1','em24_tcp'].includes(c.wallbox_meter_type);
 if(id.startsWith('house_'))return Number(c.house_rs485_interface||0)!==1&&['xemex','sdm630','sdm630mct','sdm230','sdm120'].includes(c.house_meter_type);
 if(id==='external_mode_input_pin')return !!c.external_mode_input_enabled;
 if(id==='evu_input_pin')return !!c.evu_input_enabled;
 if(id==='relay1_pin')return !!c.relay_board_enabled&&Number(c.phase_actuator_type||0)===0;
 return value(row)!==-1;
}
function renderGPIO(data){
 gpioRows=[];el('expert-gpio-roles')?.replaceChildren();
 if(!data.gpio)return;
 el('expert-gpio-board').textContent=data.gpio.board;
 for(const meta of data.gpio.roles){
  const input=document.createElement('select'),pins=data.gpio.pins.filter(p=>meta.led?p.led_allowed:!p.reason);
  if(meta.id.startsWith('phase_feedback'))continue;
  if(meta.led)pins.unshift({pin:-1});
  for(const p of pins){const option=document.createElement('option');option.value=String(p.pin);option.textContent=p.pin===-1?'Aus':`GPIO ${p.pin}`;input.append(option);}
  input.value=String(meta.value);input.setAttribute('form','expert-gpio-form');
  const row=makeRow({key:meta.id,label:meta.label,help:meta.help,standard:meta.default,factory:meta.factory??meta.default,saved:meta.value,active:meta.active,numeric:true,scale:1,gpio:true,led:meta.led},input);
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
  const c=configBaseline||snapshot?.settings||{},bus=row.key.startsWith('wallbox_')?'shell':row.key.startsWith('xemex_')?'meter':row.key.startsWith('house_')?'house':null;
  const fixed=bus&&Number(c[bus+'_rs485_interface'])===2;row.input.disabled=row.reset.disabled=!editable||fixed;if(row.adopt)row.adopt.disabled=!editable||fixed||changed(row)||!same(value(row),row.active)||same(value(row),row.standard);
  const others=(occupied.get(value(row))||[]).filter(label=>label!==row.label);
  const modified=(gpioUsed(row)||row.led||changed(row))&&!same(value(row),row.standard),invalid=value(row)!==-1&&others.length>0&&(gpioUsed(row)||changed(row));
  conflicts+=Number(invalid);row.input.setCustomValidity(invalid?'GPIO ist mehrfach belegt. Eine andere freie Belegung wählen.':'');
  row.input.setAttribute('aria-description',gpioUsed(row)?'Aktive GPIO-Belegung':'Funktion aus. Diese Vormerkung belegt noch keinen GPIO.');
  row.node.classList.toggle('is-modified',modified);row.node.classList.toggle('is-unsaved',changed(row));row.node.classList.toggle('gpio-conflict',invalid);
  for(const option of Array.from(row.input.options)){
   const pin=Number(option.value),others=(occupied.get(pin)||[]).filter(label=>label!==row.label);
   option.disabled=pin!==-1&&others.length>0;
   option.textContent=pin===-1?'Aus':`GPIO ${pin}${others.length?' · belegt: '+others.join(', '):pin===row.standard?' · Standard':''}${!gpioUsed(row)&&pin===value(row)?' · Funktion aus':''}`;
  }
 }
 const dirty=gpioRows.filter(changed).length;
 el('expert-gpio-note').textContent=conflicts?'Doppelte GPIO-Belegung korrigieren.':dirty?`${dirty} GPIO-Änderung${dirty===1?'':'en'} noch nicht gespeichert.`:editable?'Keine ungespeicherten GPIO-Änderungen.':'GPIOs können nur bei gestoppter Ladung geändert werden.';
 el('expert-gpio-note').parentElement.classList.toggle('has-edits',!!dirty);
 el('expert-gpio-save').disabled=!editable||!dirty||!!conflicts;el('expert-gpio-defaults').disabled=!editable;
 const inventory=el('expert-gpio-inventory');inventory.replaceChildren();
 for(const p of snapshot.gpio.pins){
  const roles=gpioRows.filter(row=>gpioUsed(row)&&value(row)===p.pin),line=document.createElement('div');line.className='gpio-pin-line';
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
