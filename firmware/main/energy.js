/* Display-only estimate: never overwrite actual measurements or zero-current proof. */
function wallboxDisplayReading(s={},online=true){
 const finite=Number.isFinite;
 const http=s.shell_http_control_enabled===true||s.http_control_trial_armed===true||s.http_control_regulation_source==='shell_http_pilot';
 const actual=s.actual_a||s.meter_a||[],measured=online&&s.feedback_ok===true&&actual.length===3&&actual.every(a=>finite(a)&&a>=0);
 const stopped=http&&(s.enabled===false||s.stop_requested===true||s.charge_stop_latched===true||s.http_control_trial_fault===true||s.runtime_watchdog_failed===true||s.firmware_check_failed===true||s.phase_switching===true||s.phase_state==='fehler'||s.mode==='off');
 const values=s.wallbox_estimated_a,age=s.wallbox_estimate_age_ms;
 const predicted=online&&s.wallbox_estimate_valid===true&&s.wallbox_estimate_source==='http_pilot'&&finite(age)&&age>=0&&age<4000&&Array.isArray(values)&&values.length===3&&values.every(a=>finite(a)&&a>=0)&&finite(s.wallbox_estimated_w)&&s.wallbox_estimated_w>=0;
 if(http){
  const valid=online&&(stopped||predicted),phase_a=valid?(stopped?[0,0,0]:values):null,power_w=valid?(stopped?0:s.wallbox_estimated_w):null;
  return {http:true,estimated:true,valid,stopped,phase_a,power_w,charging:valid&&!stopped&&Math.max(...phase_a)>=1,
   note:!online?'Schätzung nicht aktuell':stopped?'Geschätzt · Stoppvorgabe':predicted?'Geschätzt · Wallbox-HTTP':'Schätzung nicht verfügbar'};
 }
 const watts=s.wallbox_w??s.estimate_w;
 return {http:false,estimated:measured&&s.wallbox_w==null,valid:measured&&finite(watts),stopped:measured&&Math.max(...actual)<1,
  phase_a:measured?actual:null,power_w:measured&&finite(watts)?Math.max(0,watts):null,charging:measured&&Math.max(...actual)>=1,
  note:!measured?'Messwerte fehlen':s.wallbox_w==null?'Aus Strommessung geschätzt':'Gemessene Wirkleistung'};
}
if(typeof window!=='undefined')window.TeeNetWallboxDisplay={reading:wallboxDisplayReading};

/* Read-only illustration. All control actions use the existing dashboard. */
(()=>{
 'use strict';
 const finite=Number.isFinite;
 function model(s,online){
  const extended=!s.basic_mode;
  const gridVisible=extended&&!!(s.zero_feed_enabled||s.grid_guard_enabled);
  const currents=s.actual_a||s.meter_a||[];
  const measured=online&&s.feedback_ok&&currents.length===3&&currents.every(a=>finite(a)&&a>=0);
  const charging=!!(measured&&Math.max(...currents)>=1);
  const grid=gridVisible&&online&&s.house_meter_ok&&finite(s.house_power_w)?s.house_power_w:null;
  const pv=extended&&s.pv_display_enabled&&online&&finite(s.pv_generation_w)?Math.max(0,s.pv_generation_w):null;
  const battery=extended&&s.battery_protect&&online&&s.battery_power_ok!==false&&finite(s.battery_charge_w)&&finite(s.battery_discharge_w)?s.battery_charge_w-s.battery_discharge_w:null;
  const display=wallboxDisplayReading(s,online);
  const measuredCarPower=measured&&finite(s.wallbox_w??s.estimate_w)?Math.max(0,s.wallbox_w??s.estimate_w):null;
  const carPower=display.http?display.power_w:measuredCarPower;
  // The house meter reports the exchange with the grid. The remaining power is
  // the home's own consumption, so every displayed flow closes the balance.
  const balance=grid!==null&&pv!==null&&battery!==null&&measuredCarPower!==null?pv+grid-battery-measuredCarPower:null;
  const houseLoad=balance!==null&&balance>=0?balance:null;
  // Asynchronous meters can briefly disagree when charging starts. Keep the
  // configured house in place; unknown power is neither zero nor absent equipment.
  const houseVisible=gridVisible;
  const houseNote=houseLoad!==null?'ohne Wallbox':balance!==null?'Messwerte werden abgeglichen':'Messwerte fehlen';
  const soc=extended&&s.battery_protect&&online&&(s.battery_soc_ok??s.battery_ok)&&finite(s.battery_soc_pct)?Math.max(0,Math.min(100,s.battery_soc_pct)):null;
  const profileSoc=s.vehicle_profile_active?s.vehicle_profile_soc_pct:s.vehicle_profiles_enabled?null:s.vehicle_soc_enabled?s.car_soc_pct:null;
  const carSoc=extended&&online&&finite(profileSoc)?Math.max(0,Math.min(100,profileSoc)):null;
  const gridFlow=grid===null?'unknown':grid>50?'import':grid< -50?'export':'idle';
  const batteryFlow=battery===null?'unknown':battery>50?'charge':battery< -50?'discharge':'idle';
  const scene=gridFlow==='import'?'grid':gridFlow==='export'?'solar':gridFlow==='unknown'?'unknown':batteryFlow==='discharge'?'battery':'self';
  // Emotions reflect the measured source mix, not a few watts of grid import.
  // A 40–60% mixed range stays neutral instead of flipping around 50%.
  const sourcesKnown=pv!==null&&grid!==null&&(!s.battery_protect||battery!==null);
  const supply=sourcesKnown?pv+Math.max(0,grid)+Math.max(0,-(battery??0)):0;
  const solarDominant=supply>50&&pv/supply>=.6;
  const gridDominant=supply>50&&Math.max(0,grid)/supply>=.6;
  const houseMood=solarDominant?'happy':gridDominant?'sad':'neutral';
  return {grid,pv,battery,carPower,houseLoad,houseVisible,houseNote,soc,carSoc,gridVisible,gridFlow,batteryFlow,scene,houseMood,charging,measured,display,
   phaseCurrents:measured?currents:null,currentsEstimated:measured&&!!s.power_is_extrapolated,
   solarVisible:pv!==null,batteryVisible:soc!==null||battery!==null,
   pvFlow:pv!==null&&pv>50,pvNight:pv!==null&&pv<100,
   // Presence and charge level are independent. Never infer presence from SOC.
   carVisible:charging||!(online&&s.vehicle_status_known&&s.vehicle_connection_state==='disconnected'),
   carHappy:charging&&solarDominant};
 }
 if(typeof module==='object'&&module.exports)module.exports={model,wallboxDisplayReading};
 if(typeof document==='undefined')return;
 const root=document.getElementById('teenet-house-mood');if(!root)return;
 const find=selector=>root.querySelector(selector),write=(selector,value)=>{const node=find(selector);if(node)node.textContent=value;};
 const hide=(selector,hidden)=>root.querySelectorAll(selector).forEach(el=>el.toggleAttribute('hidden',hidden));
 const number=(value,digits=1)=>finite(value)?value.toLocaleString('de-DE',{minimumFractionDigits:digits,maximumFractionDigits:digits}):'—';
 const power=value=>number(value===null?null:Math.abs(value)/1000,2);
 function route(name,flow,x,consumer=false){
  const group=find('[data-route="'+name+'"]');group.dataset.flow=flow;
  if(x!==undefined){
   for(const path of group.querySelectorAll('.en-wire,.en-motion'))path.setAttribute('d','M'+x+(consumer?' 45V100':' 0V45'));
   group.querySelector('.en-arrow').setAttribute('d','m'+(x-4)+(consumer?' 72':' 18')+' 4 5 4-5');
   group.querySelector('.en-arrow').setAttribute('transform',flow==='reverse'?'rotate(180 '+x+' 20.5)':'');
  }
 }
 const compactMode=document.getElementById('compact-charge-mode');
 const batteryOptions=document.getElementById('battery-options');
 compactMode?.addEventListener('change',()=>{
  if(compactMode.disabled)return;
  const button=document.querySelector('[data-mode="'+compactMode.value+'"]');
  if(button&&!button.disabled)button.click();
  compactMode.value=document.getElementById('mode').value;
 });
 let current=null,viewRevision=0,densityRevision=0;
 function update(s,online){
  const profile=window.teenetVehicles?.energyStatus?.();
  if(profile)s={...s,vehicle_profile_active:!!profile.active,vehicle_profiles_enabled:!!profile.profilesEnabled,vehicle_profile_soc_pct:profile.active?profile.soc:null};
  current={s,online};
  const m=model(s,online);
  const text=id=>document.getElementById(id)?.textContent||'—';
  const mode=document.getElementById('mode').value;
  compactMode.value=mode;
  compactMode.disabled=!online||!!s.evcc_test_enabled||!!s.firmware_check_pending||document.getElementById('apply-control').disabled;
  compactMode.querySelector('[value="pv"]').disabled=!!s.basic_mode||s.zero_feed_enabled===false;
  document.querySelector('[data-charge-live]').textContent=power(m.carPower);
  document.querySelector('[data-charge-target]').textContent=document.getElementById('charge-target').textContent;
  const stateBadge=document.getElementById('compact-charge-state');
  stateBadge.textContent=text('charging');
  stateBadge.className=document.getElementById('charging').className+' standard-only';
  document.getElementById('compact-energy-today').textContent=text('today')+' kWh';
  document.getElementById('compact-energy-time').textContent=text('coverage');
  document.getElementById('compact-car-soc-row').hidden=document.getElementById('car-soc-row').hidden;
  document.getElementById('compact-car-soc').textContent=text('car-soc')+' %';
  const powerNote=document.getElementById('compact-power-note'),sourceNote=text('power-note');
  powerNote.textContent=sourceNote;powerNote.hidden=['Gemessene Wirkleistung','Keine Ladung','—'].includes(sourceNote);
  const vehicleNote=document.getElementById('compact-vehicle-note');
  vehicleNote.textContent=document.getElementById('vehicle-state')?.textContent||'';vehicleNote.hidden=document.getElementById('vehicle-state-row')?.hidden??true;
  const batterySoc=document.getElementById('compact-battery-soc'),socValue=text('battery-soc');
  batterySoc.textContent=socValue+' %'+(document.getElementById('battery-soc').dataset.stale==='true'&&socValue!=='—'?' · veraltet':'');
  if(document.documentElement.dataset.dashboardView!=='graphic')return;
  Object.assign(root.dataset,{scene:m.scene,houseMood:m.houseMood,solarVisible:String(m.solarVisible),carVisible:String(m.carVisible),carHappy:String(m.carHappy),charging:String(m.charging),gridFlow:m.gridFlow,batteryFlow:m.batteryFlow,pvFlow:String(m.pvFlow),pvNight:String(m.pvNight),paused:String(document.hidden||!online)});
  write('[data-number]',power(m.grid));
  write('[data-direction]',{unknown:'Keine Messwerte',import:'Bezug',export:'Einspeisung',idle:'Ausgeglichen'}[m.gridFlow]);
  write('[data-pv-value]',power(m.pv));
  write('[data-battery-label]',{charge:'Lädt',discharge:'Entlädt',idle:'Ruht',unknown:'Keine Messwerte'}[m.batteryFlow]);
  write('[data-battery-power]',power(m.battery));
  write('[data-house-load]',power(m.houseLoad));
  write('[data-house-note]',m.houseNote);
  write('.en-battery-soc',number(m.soc,0)+(m.soc===null?'':' %'));
  const fill=59*(m.soc??0)/100;
  find('.en-battery-level').setAttribute('height',String(fill));
  find('.en-battery-level').setAttribute('y',String(78-fill));
  write('[data-car-soc]',(profile?.active&&profile.estimated?'≈ ':'')+number(m.carSoc,0)+' %');
  const carBattery=find('[data-car-soc-block]');
  if(carBattery){carBattery.setAttribute('aria-label','Fahrzeugakku '+(profile?.active&&profile.estimated?'geschätzt ':'')+number(m.carSoc,0)+' Prozent');carBattery.querySelector('[data-car-battery-fill]')?.setAttribute('width',String(64*(m.carSoc??0)/100));}
  find('.en-wallbox-node')?.classList.toggle('en-has-soc',m.carVisible&&m.carSoc!==null);
  write('[data-car-license]','');hide('[data-car-license]',true);
  write('[data-car-name]',profile?.active?profile.name:'');hide('[data-car-name]',!online||!profile?.active);
  write('[data-car-connection]',text('vehicle-state'));hide('[data-car-connection]',document.getElementById('vehicle-state-row')?.hidden!==false);
  write('[data-wallbox-setpoint]','Soll '+text('charge-target')+' kW');
  write('[data-wallbox-phases]',s.charge_phases===1?'1-phasig':s.charge_phases===3?'3-phasig':'');
  const plan=s.charge_plan;
  const deadline=plan?.deadline_epoch?new Date(plan.deadline_epoch*1000).toLocaleTimeString('de-DE',{hour:'2-digit',minute:'2-digit'}):'';
  write('[data-wallbox-goal]',plan?.active?(profile?.target?`Ziel ${profile.target} % · ${deadline}`:`Ziel bis ${deadline}`):'');hide('[data-wallbox-goal]',!plan?.active);
  const art=find('.en-car-art'),button=art?.closest('.en-picture-button');
  if(button){let image=button.querySelector('.en-vehicle-picture');if(!image){image=document.createElement('img');image.className='en-art en-vehicle-picture';image.width=180;image.height=125;button.append(image);}const visible=online&&m.carVisible&&profile?.active&&!!profile.image;image.hidden=!visible;art.toggleAttribute('hidden',!!visible);if(visible){if(image.getAttribute('src')!==profile.image)image.src=profile.image;image.alt=profile.name||'Fahrzeug';}
   let charger=button.querySelector('.en-profile-charger');
   if(!charger){charger=document.createElementNS('http://www.w3.org/2000/svg','svg');charger.setAttribute('class','en-profile-charger');charger.setAttribute('viewBox','0 0 46 72');charger.setAttribute('aria-hidden','true');charger.innerHTML='<defs><linearGradient id="en-charger-case" x2="1" y2="1"><stop stop-color="#f4f6f7"/><stop offset="1" stop-color="#bbc5cc"/></linearGradient></defs><path d="M12 5h25v64H12z" fill="#82909b" opacity=".2"/><rect x="7" y="4" width="31" height="59" rx="8" fill="url(#en-charger-case)" stroke="#667984" stroke-width="1.4"/><rect x="12" y="10" width="21" height="43" rx="5" fill="#293d47"/><rect x="18" y="16" width="9" height="3" rx="1.5" fill="#72cbb4"/><path d="m24 26-8 12h6l-2 9 10-14h-7z" fill="#d6e9ef"/><circle cx="22.5" cy="58" r="1.4" fill="#5c7a87"/>';button.append(charger);}
   charger.toggleAttribute('hidden',false);button.classList.toggle('en-with-profile',!!visible);}

  const sources=[['solar',m.solarVisible,m.pvFlow?'forward':'idle'],['grid',m.gridVisible,{import:'forward',export:'reverse',idle:'idle',unknown:'unknown'}[m.gridFlow]],['battery',m.batteryVisible,{charge:'reverse',discharge:'forward',idle:'idle',unknown:'unknown'}[m.batteryFlow]]];
  const shown=sources.filter(item=>item[1]);
  find('.en-sources').style.setProperty('--en-source-count',String(Math.max(1,shown.length)));
  for(const [name,visible,flow] of sources){
   hide('[data-source="'+name+'"],[data-route="'+name+'"]',!visible);
   if(visible)route(name,flow,(shown.findIndex(item=>item[0]===name)+.5)*600/shown.length);
  }
  hide('.en-roof-panel',!m.solarVisible);
  hide('.en-distribution',!shown.length);
  const showHome=m.houseVisible,carX=showHome?450:300;
  hide('.en-home-node,[data-route="home"],.en-home-junction',!showHome);
  find('.en-consumers').style.setProperty('--en-consumer-count',showHome?'2':'1');
  find('.en-car-junction').setAttribute('cx',String(carX));
  const points=[carX,...(showHome?[150]:[]),...shown.map((_,i)=>(i+.5)*600/shown.length)];
  find('.en-bus').setAttribute('d','M'+Math.min(...points)+' 45H'+Math.max(...points));
  route('home',m.houseLoad===null?'unknown':m.houseLoad>50?'forward':'idle',150,true);
  route('car',m.charging?'forward':m.measured?'idle':'unknown',carX,true);
  hide('[data-car-part]',!m.carVisible);
  hide('[data-car-soc-block]',!m.carVisible||m.carSoc===null);
  write('[data-car-power]',power(m.carPower));
  write('[data-wallbox-state]',text('charging'));
  write('[data-energy-today]',text('today')+' kWh');write('[data-energy-time]',text('coverage'));
  const note=m.display.http?m.display.note:m.measured?'':'Aktuelle Wallbox-Messung fehlt';
  write('[data-car-note]',note);hide('[data-car-note]',!note);
 }
 function setDensity(density){
  density=density==='compact'?'compact':'standard';
  document.documentElement.dataset.dashboardDensity=density;
  try{localStorage.setItem('teennet-density',density);}catch{}
  for(const option of ['standard','compact'])document.getElementById('density-'+option)?.setAttribute('aria-pressed',String(density===option));
  if(batteryOptions)batteryOptions.open=true;
 }
 async function selectDensity(density){
  const revision=++densityRevision;setDensity(density);
  const note=document.getElementById('density-save-status');note.hidden=true;
  for(const option of ['standard','compact'])document.getElementById('density-'+option).disabled=true;
  try{await api('/api/theme',{density},5000);}
  catch{if(revision===densityRevision){note.textContent='Nur in diesem Browser gespeichert. Gerät derzeit nicht erreichbar oder Anmeldung erforderlich.';note.hidden=false;}}
  finally{for(const option of ['standard','compact'])document.getElementById('density-'+option).disabled=false;}
 }
 function setView(view){
  view=view==='standard'?'standard':'graphic';
  document.documentElement.dataset.dashboardView=view;
  try{localStorage.setItem('teennet-view',view);}catch{}
  for(const option of ['standard','graphic'])document.getElementById('view-'+option)?.setAttribute('aria-pressed',String(view===option));
  if(current)update(current.s,current.online);
 }
 async function selectView(view){
  const revision=++viewRevision;setView(view);
  const note=document.getElementById('view-save-status');
  note.hidden=true;
  for(const option of ['standard','graphic'])document.getElementById('view-'+option).disabled=true;
  try{await api('/api/theme',{view},5000);}
  catch{if(revision===viewRevision){note.textContent='Nur in diesem Browser gespeichert. Gerät derzeit nicht erreichbar oder Anmeldung erforderlich.';note.hidden=false;}}
  finally{for(const option of ['standard','graphic'])document.getElementById('view-'+option).disabled=false;}
 }
 window.addEventListener('teenet-vehicle-display',()=>{if(current)window.TeeNetEnergy.update(current.s,current.online);});
 window.TeeNetEnergy={update,setView,setDensity,loadDensity(density){if(!densityRevision)setDensity(density);},loadView(view){if(!viewRevision)setView(view);},loadNotice(){ /* Retired presentation announcement; tolerate old firmware. */ }};
 for(const option of ['standard','graphic'])document.getElementById('view-'+option)?.addEventListener('click',()=>selectView(option));
 for(const option of ['standard','compact'])document.getElementById('density-'+option)?.addEventListener('click',()=>selectDensity(option));
 let saved,savedDensity;try{saved=localStorage.getItem('teennet-view');savedDensity=localStorage.getItem('teennet-density');}catch{}setView(saved);setDensity(savedDensity);
 document.addEventListener('visibilitychange',()=>{root.dataset.paused=String(document.hidden||!current?.online);});
})();

/* Expand the wallbox inside the existing energy card; controls stay read-only. */
(()=>{
 'use strict';
 if(typeof document==='undefined')return;
 const root=document.getElementById('teenet-house-mood'),energy=window.TeeNetEnergy;
 const node=root?.querySelector('.en-wallbox-node');
 if(!node||!energy)return;
 const art=node.querySelector('.en-car-art');
 const picture=document.createElement('div');picture.className='en-picture-button';art.before(picture);picture.append(art);
 const open=document.createElement('button');open.type='button';open.className='en-wallbox-open';
 open.setAttribute('aria-label','Wallbox vergrößern');open.setAttribute('aria-expanded','false');open.setAttribute('aria-controls','wallbox-expanded');open.title='Wallbox und Fahrzeugdetails';node.append(open);
 const panel=document.createElement('section');panel.id='wallbox-expanded';panel.className='en-wallbox-expanded-panel';panel.hidden=true;panel.setAttribute('aria-labelledby','wallbox-expanded-title');
 panel.innerHTML='<div class="en-expanded-header"><div><h2 id="wallbox-expanded-title">Wallbox</h2><p data-expanded-status></p></div></div><div class="en-expanded-metrics"><div><span data-expanded-power-label>Istleistung</span><strong data-expanded-power>—</strong></div><div data-expanded-estimate-current-wrap hidden><span>Strom je Phase · geschätzt</span><strong data-expanded-estimate-current>—</strong></div><div><span>Aktiver Sollwert</span><strong data-expanded-target>—</strong></div><div><span>Sollstrom</span><strong data-expanded-current>—</strong></div><div><span>Vorgewählte Leistung</span><strong data-expanded-preset>—</strong></div></div><div class="en-expanded-phase-wrap"><dl class="en-expanded-phases" aria-label="Messwerte je Phase">'+[1,2,3].map(i=>'<div><dt>L'+i+'</dt><dd><span data-expanded-voltage="'+(i-1)+'">—</span><span data-expanded-amps="'+(i-1)+'">—</span></dd></div>').join('')+'</dl></div><p class="en-expanded-note" data-expanded-note></p><div class="en-expanded-today"><span>Heute geladen <b data-expanded-today>—</b></span><span>Ladezeit <b data-expanded-time>—</b></span></div><section class="en-expanded-vehicle" hidden><div class="en-expanded-vehicle-heading"><img alt="" data-expanded-vehicle-photo hidden><h3 data-expanded-vehicle-name></h3></div><dl data-expanded-vehicle-facts></dl></section>';
 root.append(panel);
 const chart=document.createElement('figure');chart.className='en-wallbox-chart';
 chart.innerHTML='<figcaption><b>Ladeverlauf</b><span><i></i> Ist <i class="en-target-key"></i> Soll</span></figcaption><svg viewBox="0 0 640 240" role="img" aria-label="Verlauf der gesamten Ladesitzung in Kilowatt"></svg><small data-curve-note>Seit Ladebeginn bis zum Ende.</small>';
 panel.insertBefore(chart,panel.querySelector('.en-expanded-metrics'));
 panel.removeAttribute('aria-labelledby');panel.setAttribute('role','button');panel.setAttribute('aria-label','Wallbox verkleinern und zur Übersicht zurückkehren');panel.tabIndex=0;let state=null,connected=false,expanded=false;
 let curve=null,historyAt=0,historyBusy=false,historyFailed=false;
 async function loadCurve(){
  if(historyBusy||document.hidden||Date.now()-historyAt<3000)return;historyBusy=true;historyAt=Date.now();
  const controller=new AbortController(),timer=setTimeout(()=>controller.abort(),5000);
  try{
   const response=await fetch('/api/charge-curve',{cache:'no-store',signal:controller.signal});
   if(!response.ok)throw new Error('Verlauf nicht verfügbar');
   const data=await response.json();
   if(!Array.isArray(data.points)||!Number.isFinite(data.duration_s)||data.duration_s<0)throw new Error('Ungültiger Verlauf');
   curve=data;historyFailed=false;
  }catch{historyFailed=true;}
  finally{clearTimeout(timer);historyBusy=false;if(expanded)drawCurve();}
 }
 function drawCurve(){
  const duration=Math.max(1,curve?.duration_s||0);
  const points=(curve?.points||[]).filter(p=>Number.isFinite(p.s)&&p.s>=0&&p.s<=duration).slice(0,257);
  const known=points.filter(p=>Number.isFinite(p.w)),range=points.filter(p=>Number.isFinite(p.low_w)&&Number.isFinite(p.high_w));
  const maximum=Math.max(2,Math.ceil(Math.max(0,...points.flatMap(p=>[p.w??0,p.target_w??0,p.high_w??0]))/2000)*2);
  const x=t=>48+t/duration*576,y=w=>202-w/1000/maximum*176;
  const path=key=>{let previous=false;return points.map(p=>{if(!Number.isFinite(p[key])){previous=false;return '';}const d=(previous?'L':'M')+x(p.s).toFixed(1)+','+y(p[key]).toFixed(1);previous=true;return d;}).join(' ');};
  const elapsed=t=>{const minutes=Math.floor(t/60);return Math.floor(minutes/60)+':'+String(minutes%60).padStart(2,'0');};
  const band=range.length?'M'+range.map(p=>x(p.s).toFixed(1)+','+y(p.high_w).toFixed(1)).join(' L')+' L'+[...range].reverse().map(p=>x(p.s).toFixed(1)+','+y(p.low_w).toFixed(1)).join(' L')+' Z':'';
  const svg=chart.querySelector('svg');svg.setAttribute('aria-label','Gesamte Ladesitzung seit Ladebeginn, Dauer '+elapsed(curve?.duration_s||0)+' Stunden, Ist und Soll in kW');
  svg.innerHTML='<g class="en-chart-grid">'+[0,.5,1].map(f=>'<path d="M48 '+(202-f*176)+'H624"/><text x="39" y="'+(206-f*176)+'" text-anchor="end">'+(maximum*f).toLocaleString('de-DE')+'</text>').join('')+'</g><g class="en-chart-labels"><text x="8" y="15">kW</text>'+[0,1,2,3].map(n=>'<text x="'+(48+n/3*576)+'" y="227" text-anchor="'+(n===3?'end':'start')+'">'+elapsed((curve?.duration_s||0)*n/3)+(n===3?' h':'')+'</text>').join('')+'</g><path class="en-chart-range" d="'+band+'"/><path class="en-chart-actual" d="'+path('w')+'"/><path class="en-chart-target" d="'+path('target_w')+'"/>'+(known.length?'<circle class="en-chart-dot" cx="'+x(known.at(-1).s).toFixed(1)+'" cy="'+y(known.at(-1).w).toFixed(1)+'" r="3"/>':'<text class="en-chart-empty" x="336" y="115" text-anchor="middle">'+(historyFailed?'Verlauf derzeit nicht verfügbar':'Noch kein Ladeverlauf')+'</text>');
  chart.querySelector('[data-curve-note]').textContent=curve?.points?.length?'Seit Ladebeginn · '+(curve.interrupted?'durch Neustart beendet':curve.active?'läuft':'beendet')+(historyFailed?' · Verbindung unterbrochen':'')+(curve.interval_s>5?' · ältere Werte zusammengefasst':'')+'.':'Neue Verläufe werden während der Ladung im Gerät erfasst.';
 }
 const format=(n,unit,digits=2)=>Number.isFinite(n)?n.toLocaleString('de-DE',{minimumFractionDigits:digits,maximumFractionDigits:digits})+' '+unit:'—';
 const put=(q,value)=>{panel.querySelector(q).textContent=value;};
 function renderVehicle(){
  const details=window.teenetVehicles?.detailsData?.(),section=panel.querySelector('.en-expanded-vehicle');section.hidden=!details;
  if(!details)return;
  panel.querySelector('[data-expanded-vehicle-name]').textContent=details.name;
  const image=panel.querySelector('[data-expanded-vehicle-photo]');image.hidden=!details.image;
  if(details.image&&image.getAttribute('src')!==details.image)image.src=details.image;
  panel.querySelector('[data-expanded-vehicle-facts]').replaceChildren(...details.rows.map(([label,value])=>{const row=document.createElement('div'),dt=document.createElement('dt'),dd=document.createElement('dd');dt.textContent=label;dd.textContent=value;row.append(dt,dd);return row;}));
 }
 function render(){
  if(!expanded)return;
  drawCurve();renderVehicle();if(connected)loadCurve();
  const s=state||{},fresh=connected&&s.feedback_ok,display=wallboxDisplayReading(s,connected);
  put('[data-expanded-status]',connected?(document.getElementById('charging')?.textContent||'—')+' · '+(s.charge_phases===1?'1-phasig':s.charge_phases===3?'3-phasig':'Phasen unbekannt'):'Gerät nicht erreichbar');
  put('[data-expanded-power-label]',display.http?'Leistung · geschätzt':'Istleistung');
  put('[data-expanded-power]',format(display.http?(display.power_w===null?null:display.power_w/1000):fresh?(s.wallbox_w??s.estimate_w)/1000:null,'kW'));
  panel.querySelector('[data-expanded-estimate-current-wrap]').hidden=!display.http;
  put('[data-expanded-estimate-current]',display.phase_a&&[1,3].includes(s.charge_phases)?display.phase_a.slice(0,s.charge_phases).map(a=>format(a,'A',1)).join(' / '):'—');
  const factor=s.power_per_amp_kw??(s.charge_phases===1?s.single_power_per_amp_kw:s.three_power_per_amp_kw);
  put('[data-expanded-target]',format(connected?s.target_current_a*factor:null,'kW'));
  put('[data-expanded-current]',format(connected?s.target_current_a:null,'A'));
  put('[data-expanded-preset]',format(connected?s.manual_power_kw:null,'kW'));
  const voltages=connected&&s.meter_ok&&['wallbox_meter','private'].includes(s.charging_voltage_source)?s.charging_voltage_v:[];
  for(let i=0;i<3;i++){
   put('[data-expanded-voltage="'+i+'"]',format(voltages?.[i],'V',1));
   put('[data-expanded-amps="'+i+'"]',format(fresh?(s.actual_a||s.meter_a)?.[i]:null,'A'));
  }
  put('[data-expanded-note]',display.http?display.note+(fresh?' · Phasenwerte: gemessen.':' · Keine unabhängigen Phasenmesswerte.'):!connected?'Keine aktuellen Messwerte.':!fresh?'Aktuelle Wallbox-Strommessung fehlt.':s.power_is_extrapolated?'Strom bzw. Leistung teilweise hochgerechnet.':!voltages?.length?'Der gewählte Zähler liefert keine aktuellen Phasenspannungen.':'');
  panel.querySelector('[data-expanded-note]').hidden=!panel.querySelector('[data-expanded-note]').textContent;
  put('[data-expanded-today]',connected?(document.getElementById('today')?.textContent||'—')+' kWh':'—');
  put('[data-expanded-time]',connected?(document.getElementById('coverage')?.textContent||'—'):'—');
 }
 function setExpanded(value,restore=true){
  expanded=value;root.classList.toggle('en-wallbox-expanded',value);panel.hidden=!value;open.setAttribute('aria-expanded',String(value));
  for(const child of root.children)if(child!==panel)child.inert=value;
  if(value){render();panel.focus({preventScroll:true});}else if(restore&&open.getClientRects().length)open.focus({preventScroll:true});
 }
 open.addEventListener('click',()=>setExpanded(true));panel.addEventListener('click',()=>setExpanded(false));
 panel.addEventListener('keydown',e=>{if(e.key==='Escape'||e.key==='Enter'||e.key===' '){e.preventDefault();setExpanded(false);}});
 const hideOnNavigate=()=>{if(expanded&&(document.documentElement.dataset.dashboardView!=='graphic'||location.hash&&location.hash!=='#overview'))setExpanded(false,false);};
 addEventListener('hashchange',hideOnNavigate);
 new MutationObserver(hideOnNavigate).observe(document.documentElement,{attributes:true,attributeFilter:['data-dashboard-view']});
 addEventListener('teenet-vehicle-display',()=>{if(expanded)renderVehicle();});
 const previous=energy.update;energy.update=function(s,online){previous.call(this,s,online);state=s;connected=online;render();};
})();

/* Vehicle charge level travels with the car, as a small battery trailer. */
(()=>{
 if(typeof document==='undefined')return;
 const root=document.getElementById('teenet-house-mood'),node=root?.querySelector('.en-wallbox-node'),picture=node?.querySelector('.en-picture-button'),battery=node?.querySelector('[data-car-soc-block]'),art=picture?.querySelector('.en-car-art');
 if(!picture||!battery||!art)return;
 const car=art.querySelector('g[data-car-part]');if(car){art.replaceChildren(car);art.setAttribute('viewBox','47 40 127 84');}
 battery.classList.add('en-battery-trailer');picture.append(battery);
 for(const name of ['bed','frame','mudguard','wheel','light']){const item=document.createElement('span');item.className='en-trailer-'+name;item.setAttribute('aria-hidden','true');battery.append(item);}
 const tow=document.createElement('span'),hitch=document.createElement('span');tow.className='en-trailer-tow';hitch.className='en-trailer-hitch';tow.setAttribute('aria-hidden','true');hitch.setAttribute('aria-hidden','true');picture.append(tow,hitch);
 const cable=document.createElementNS('http://www.w3.org/2000/svg','svg');cable.setAttribute('class','en-charging-cable');cable.setAttribute('aria-hidden','true');cable.innerHTML='<path class="en-charging-lead" fill="none" stroke-linecap="round"/><rect class="en-charging-plug" x="-5" y="-3" width="7" height="6" rx="1.5"/>';picture.append(cable);
 let pending=false;
 function layout(){
  pending=false;const width=picture.clientWidth;if(!width)return;
  const hasCar=root.dataset.carVisible!=='false',hasSoc=hasCar&&!battery.hidden;
  picture.classList.toggle('en-has-trailer',hasSoc);
  const photo=picture.querySelector('.en-vehicle-picture'),active=photo&&!photo.hidden?photo:art;
  const trailerWidth=Math.min(94,Math.max(34,width*.23)),left=width*.21,trailerLeft=width-trailerWidth-2;
  const carWidth=hasSoc?Math.min(width*.535,(trailerLeft-left-5)/.97):width*.70;
  picture.style.height=Math.max(76,Math.min(190,width*.49))+'px';
  for(const image of [art,photo])if(image){image.style.width=carWidth+'px';image.style.left=(hasSoc?left:width*.25)+'px';}
  const scene=picture.getBoundingClientRect(),rect=active.getBoundingClientRect(),profile=active===photo;
  const x=rect.left-scene.left+rect.width*(profile ? .969 : .937),y=rect.top-scene.top+rect.height*(profile ? .555 : .702),ground=rect.top-scene.top+rect.height*(profile ? .86 : .905);
  const bodyHeight=Math.min(58,Math.max(26,width*.14)),wheel=Math.min(25,Math.max(12,width*.06)),frameY=Math.min(y+10,ground-wheel/2-3),top=frameY-bodyHeight;
  Object.assign(battery.style,{left:trailerLeft+'px',top:top+'px',width:trailerWidth+'px',height:Math.max(bodyHeight,ground-top)+'px'});
  battery.style.setProperty('--trailer-body',bodyHeight+'px');battery.style.setProperty('--trailer-wheel',wheel+'px');battery.style.setProperty('--trailer-suspension',Math.max(2,ground-frameY-wheel/2)+'px');
  Object.assign(tow.style,{left:x+'px',top:y+'px',width:Math.hypot(trailerLeft-x,frameY-y)+'px',transform:`rotate(${Math.atan2(frameY-y,trailerLeft-x)}rad)`});
  Object.assign(hitch.style,{left:(x-2)+'px',top:(y-2)+'px'});
  const charger=picture.querySelector('.en-profile-charger');
  cable.toggleAttribute('hidden',!hasCar||!charger);
  if(charger){
   const size=Math.min(58,Math.max(23,width*.14));Object.assign(charger.style,{width:size+'px',height:size*72/46+'px',left:hasCar?'0px':(width-size)/2+'px',bottom:(scene.height-ground+Math.max(6,width*.05))+'px'});
   if(hasCar){
    const box=charger.getBoundingClientRect(),sx=box.left-scene.left+size*38/46,sy=box.top-scene.top+size*39/46;
    const ex=rect.left-scene.left+rect.width*(profile ? .10 : .09),ey=rect.top-scene.top+rect.height*(profile ? .60 : .62),loop=Math.min(scene.height-2,Math.max(sy+9,ey+9,ground-4)),mid=(sx+ex)/2;
    cable.setAttribute('viewBox',`0 0 ${width} ${scene.height}`);
    cable.querySelector('path').setAttribute('d',`M${sx} ${sy}C${sx+6} ${sy} ${sx+2} ${loop} ${mid} ${loop}S${ex-6} ${ey} ${ex} ${ey}`);
    cable.querySelector('path').setAttribute('stroke-width',Math.max(1.8,Math.min(3,width*.011)));
    cable.querySelector('rect').setAttribute('transform',`translate(${ex} ${ey})`);
   }
  }
 }
 const schedule=()=>{if(!pending){pending=true;requestAnimationFrame(layout);}};
 new ResizeObserver(schedule).observe(picture);picture.addEventListener('load',schedule,true);
 window.addEventListener('teenet-status',schedule);window.addEventListener('teenet-vehicle-display',schedule);schedule();
})();

/* Optional measured heat-pump consumption; read-only display. */
(()=>{
 'use strict';
 const finite=Number.isFinite;
 function model(s,online){
  const enabled=!s.basic_mode&&s.heatpump_enabled===true;
  const power=enabled&&online&&s.heatpump_ok===true&&finite(s.heatpump_w)&&s.heatpump_w>=0?s.heatpump_w:null;
  const currents=s.actual_a||s.meter_a||[];
  const measured=online&&s.feedback_ok&&currents.length===3&&currents.every(a=>finite(a)&&a>=0);
  const grid=!s.basic_mode&&(s.zero_feed_enabled||s.grid_guard_enabled)&&online&&s.house_meter_ok&&finite(s.house_power_w)?s.house_power_w:null;
  const pv=!s.basic_mode&&s.pv_display_enabled&&online&&finite(s.pv_generation_w)?Math.max(0,s.pv_generation_w):null;
  const battery=!s.basic_mode&&s.battery_protect&&online&&s.battery_power_ok!==false&&finite(s.battery_charge_w)&&finite(s.battery_discharge_w)?s.battery_charge_w-s.battery_discharge_w:null;
  const wallbox=measured&&finite(s.wallbox_w??s.estimate_w)?Math.max(0,s.wallbox_w??s.estimate_w):null;
  const combined=[grid,pv,battery,wallbox].every(value=>value!==null)?pv+grid-battery-wallbox:null;
  const remainder=power!==null&&combined!==null?combined-power:null;
  // An unknown submeter must not look like zero, nor produce a fictitious remainder.
  const houseLoad=remainder!==null&&remainder>=0?remainder:null;
  const houseVisible=!s.basic_mode&&!!(s.zero_feed_enabled||s.grid_guard_enabled);
  const houseNote=houseLoad!==null?'ohne Wallbox und Wärmepumpe':remainder!==null?'Messwerte werden abgeglichen':'Messwerte fehlen';
  return {enabled,power,houseLoad,houseVisible,houseNote,flow:power===null?'unknown':power>50?'forward':'idle'};
 }

 if(typeof document==='undefined'||!document.body||!window.TeeNetEnergy)return;
 if(document.getElementById('heatpump-energy-node'))return;
 const root=document.getElementById('teenet-house-mood');
 const consumers=root?.querySelector('.en-consumers'),paths=root?.querySelector('.en-paths');
 if(!consumers||!paths)return;
 const icon='<svg class="en-art en-heatpump-art" viewBox="0 0 160 125" aria-hidden="true"><ellipse cx="80" cy="115" rx="55" ry="4" fill="var(--en-shadow)"/><path d="M111 73h17v29h-17" fill="none" stroke="var(--en-heatpump)" stroke-width="4" stroke-linejoin="round"/><rect x="25" y="34" width="91" height="72" rx="9" fill="var(--en-surface)" stroke="var(--en-heatpump)" stroke-width="3"/><path d="M37 106v7m67-7v7" stroke="var(--en-heatpump)" stroke-width="5" stroke-linecap="round"/><circle cx="64" cy="70" r="24" fill="var(--en-bg)" stroke="var(--en-line)" stroke-width="2"/><g class="en-heatpump-fan" fill="var(--en-heatpump)"><path d="M64 66c-4-16 4-21 12-16 7 5 2 15-8 20zM68 70c16-4 21 4 16 12-5 7-15 2-20-8zM64 74c4 16-4 21-12 16-7-5-2-15 8-20zM60 70c-16 4-21-4-16-12 5-7 15-2 20 8z"/><circle cx="64" cy="70" r="4" fill="var(--en-surface)"/></g><path d="M98 48h7m-7 8h7m-7 8h7m-7 8h7m-7 8h7m-7 8h7" stroke="var(--en-line)" stroke-width="2" stroke-linecap="round"/><path d="M44 25q-6-6 0-12m17 12q-6-6 0-12m17 12q-6-6 0-12" fill="none" stroke="var(--en-heatpump)" stroke-width="2.5" stroke-linecap="round"/></svg>';
 const node=document.createElement('div');
 node.id='heatpump-energy-node';node.className='en-node en-heatpump-node';node.hidden=true;
 node.innerHTML=icon+'<span class="en-label">Wärme<wbr>pumpe</span><strong class="en-power"><span data-heatpump-value>—</span><small>kW</small></strong><span class="en-detail" data-heatpump-state>Keine Messwerte</span>';
 consumers.insertBefore(node,consumers.querySelector('.en-wallbox-node'));
 const svgNS='http://www.w3.org/2000/svg';
 const route=document.createElementNS(svgNS,'g');route.dataset.route='heatpump';route.classList.add('en-route');route.setAttribute('hidden','');
 for(const cls of ['en-wire','en-motion','en-arrow']){const path=document.createElementNS(svgNS,'path');path.setAttribute('class',cls);if(cls==='en-motion')path.setAttribute('pathLength','100');route.append(path);}
 paths.append(route);
 const junction=document.createElementNS(svgNS,'circle');junction.setAttribute('class','en-junction en-heatpump-junction');junction.setAttribute('cy','45');junction.setAttribute('r','4');junction.setAttribute('hidden','');paths.append(junction);
 const originalUpdate=window.TeeNetEnergy.update;
 let current=null;
 const power=value=>value===null?'—':(value/1000).toLocaleString('de-DE',{minimumFractionDigits:2,maximumFractionDigits:2});
 function consumerRoute(name,x,flow){
  const group=paths.querySelector('[data-route="'+name+'"]');group.dataset.flow=flow;
  for(const path of group.querySelectorAll('.en-wire,.en-motion'))path.setAttribute('d','M'+x+' 45V100');
  group.querySelector('.en-arrow').setAttribute('d','m'+(x-4)+' 72 4 5 4-5');
  paths.querySelector('.en-'+name+'-junction')?.setAttribute('cx',String(x));
 }
 function paint(s,online){
  const m=model(s,online);
  root.dataset.heatpumpEnabled=String(m.enabled);
  root.dataset.heatpumpRunning=String(m.flow==='forward');
  node.hidden=!m.enabled;
  route.toggleAttribute('hidden',!m.enabled);junction.toggleAttribute('hidden',!m.enabled);
  // The base renderer has a fixed home junction and only moves the wallbox junction.
  paths.querySelector('.en-home-junction').setAttribute('cx','150');
  if(!m.enabled)return;
  const state=m.power===null?'Keine Messwerte':m.power>50?'Verbrauch':'Ruht';
  node.querySelector('[data-heatpump-value]').textContent=power(m.power);
  node.querySelector('[data-heatpump-state]').textContent=state;
  if(document.documentElement.dataset.dashboardView!=='graphic')return;
  const showHome=m.houseVisible;
  root.querySelector('.en-home-node').hidden=!showHome;
  paths.querySelector('[data-route="home"]').toggleAttribute('hidden',!showHome);
  paths.querySelector('.en-home-junction').toggleAttribute('hidden',!showHome);
  root.querySelector('[data-house-load]').textContent=power(m.houseLoad);
  root.querySelector('[data-house-note]').textContent=m.houseNote;
  consumers.style.setProperty('--en-consumer-count',showHome?'3':'2');
  const hpX=showHome?300:150,carX=showHome?500:450;
  if(showHome)consumerRoute('home',100,m.houseLoad===null?'unknown':m.houseLoad>50?'forward':'idle');
  consumerRoute('heatpump',hpX,m.flow);
  const carFlow=paths.querySelector('[data-route="car"]').dataset.flow;
  consumerRoute('car',carX,carFlow);
  const sourceCount=root.querySelectorAll('.en-sources [data-source]:not([hidden])').length;
  const points=[hpX,carX,...(showHome?[100]:[]),...Array.from({length:sourceCount},(_,i)=>(i+.5)*600/sourceCount)];
  paths.querySelector('.en-bus').setAttribute('d','M'+Math.min(...points)+' 45H'+Math.max(...points));
 }
 function update(s,online){
  current={s,online};originalUpdate.call(window.TeeNetEnergy,s,online);paint(s,online);
 }
 window.TeeNetEnergy.update=update;
 // The base view selector rerenders via its private function. Reapply this demo extension.
 const observer=new MutationObserver(()=>{if(current)update(current.s,current.online);});
 observer.observe(document.documentElement,{attributes:true,attributeFilter:['data-dashboard-view','data-dashboard-density']});
 window.TeeNetHeatpumpEnergy={refresh(){if(current)update(current.s,current.online);}};
})();

/* Keep energy paths aligned with visible sources and consumers. */
(()=>{
 'use strict';
 if(typeof document==='undefined')return;
 const root=document.getElementById('teenet-house-mood'),energy=window.TeeNetEnergy;
 if(!root||!energy||typeof ResizeObserver==='undefined'||energy.routeLayoutInstalled)return;
 energy.routeLayoutInstalled=true;
 const overview=root.querySelector('.en-overview');
 // Read the actual card centres, including grid gaps and hidden functions.
 // Extend source leads to their labels instead of leaving a gap below short cards.
 let layoutPending=false;
 function alignRoutes(){
  layoutPending=false;
  if(document.documentElement.dataset.dashboardView!=='graphic')return;
  const paths=root.querySelector('.en-paths'),bounds=paths.getBoundingClientRect();
  if(!bounds.width||!bounds.height)return;
  const entries=[['solar','[data-source="solar"]'],['grid','[data-source="grid"]'],['battery','[data-source="battery"]'],
   ['home','.en-home-node',true],['heatpump','#heatpump-energy-node',true],['car','.en-wallbox-node',true]];
  const measured=[];
  for(const [name,selector,consumer] of entries){
   const node=root.querySelector(selector),route=paths.querySelector('[data-route="'+name+'"]');
   if(!node||node.hidden||!route||route.hasAttribute('hidden'))continue;
   const box=node.getBoundingClientRect();
   if(!box.width||!box.height)continue;
   let end=consumer?box:node.lastElementChild.getBoundingClientRect();
   // Compact mode hides the battery's edit link via CSS.
   if(!end.width||!end.height)end=node.querySelector('.en-detail')?.getBoundingClientRect()||box;
   const x=(box.left+box.width/2-bounds.left)*600/bounds.width;
   const start=consumer?45:(end.bottom+7-bounds.top)*100/bounds.height;
   const finish=consumer?(box.top-bounds.top)*100/bounds.height:45;
   measured.push({name,route,x,start,finish,consumer});
  }
  const rounded=value=>Math.round(value*100)/100;
  for(const {name,route,x,start,finish,consumer} of measured){
   const px=rounded(x),d='M'+px+' '+rounded(start)+'V'+rounded(finish);
   for(const path of route.querySelectorAll('.en-wire,.en-motion'))path.setAttribute('d',d);
   const arrow=route.querySelector('.en-arrow'),y=rounded((start+finish)/2-2.5);
   arrow.setAttribute('d','m'+rounded(px-4)+' '+y+' 4 5 4-5');
   arrow.setAttribute('transform',route.dataset.flow==='reverse'?'rotate(180 '+px+' '+rounded(y+2.5)+')':'');
   if(consumer)paths.querySelector('.en-'+name+'-junction')?.setAttribute('cx',String(px));
  }
  if(measured.length)paths.querySelector('.en-bus').setAttribute('d','M'+rounded(Math.min(...measured.map(p=>p.x)))+' 45H'+rounded(Math.max(...measured.map(p=>p.x))));
 }
 function scheduleLayout(){
  if(!layoutPending){layoutPending=true;requestAnimationFrame(alignRoutes);}
 }
 new ResizeObserver(scheduleLayout).observe(overview);
 const previous=energy.update;
 energy.update=function(s,online){previous.call(this,s,online);scheduleLayout();};
 new MutationObserver(scheduleLayout).observe(document.documentElement,{attributes:true,attributeFilter:['data-dashboard-view','data-dashboard-density']});
})();

