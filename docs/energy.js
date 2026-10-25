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
  const carPower=measured&&finite(s.wallbox_w??s.estimate_w)?Math.max(0,s.wallbox_w??s.estimate_w):null;
  // The house meter reports the exchange with the grid. The remaining power is
  // the home's own consumption, so every displayed flow closes the balance.
  const balance=grid!==null&&pv!==null&&battery!==null&&carPower!==null?pv+grid-battery-carPower:null;
  const houseLoad=balance!==null&&balance>=0?balance:null;
  // Asynchronous meters can briefly disagree when charging starts. Keep the
  // configured house in place; unknown power is neither zero nor absent equipment.
  const houseVisible=gridVisible;
  const houseNote=houseLoad!==null?'ohne Wallbox':balance!==null?'Messwerte werden abgeglichen':'Messwerte fehlen';
  const soc=extended&&s.battery_protect&&online&&(s.battery_soc_ok??s.battery_ok)&&finite(s.battery_soc_pct)?Math.max(0,Math.min(100,s.battery_soc_pct)):null;
  const carSoc=extended&&s.vehicle_soc_enabled&&online&&finite(s.car_soc_pct)?Math.max(0,Math.min(100,s.car_soc_pct)):null;
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
  return {grid,pv,battery,carPower,houseLoad,houseVisible,houseNote,soc,carSoc,gridVisible,gridFlow,batteryFlow,scene,houseMood,charging,measured,
   phaseCurrents:measured?currents:null,currentsEstimated:measured&&!!s.power_is_extrapolated,
   solarVisible:pv!==null,batteryVisible:soc!==null||battery!==null,
   pvFlow:pv!==null&&pv>50,pvNight:pv!==null&&pv<100,
   // Presence and charge level are independent. Never infer presence from SOC.
   carVisible:charging||!(online&&s.vehicle_status_known&&s.vehicle_connection_state==='disconnected'),
   carHappy:charging&&solarDominant};
 }
 if(typeof module==='object'&&module.exports)module.exports={model};
 if(typeof document==='undefined')return;
 const root=document.getElementById('teenet-house-mood');if(!root)return;
 const find=selector=>root.querySelector(selector),write=(selector,value)=>{find(selector).textContent=value;};
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
 const batteryOptions=document.getElementById('battery-options'),batteryOpen=document.getElementById('graphic-battery-open');
 compactMode?.addEventListener('change',()=>{
  if(compactMode.disabled)return;
  const button=document.querySelector('[data-mode="'+compactMode.value+'"]');
  if(button&&!button.disabled)button.click();
  compactMode.value=document.getElementById('mode').value;
 });
 batteryOpen?.addEventListener('click',()=>{
  batteryOptions.open=true;
  document.getElementById('battery-title').focus({preventScroll:true});
  batteryOptions.scrollIntoView({block:'nearest',behavior:'smooth'});
 });
 let current=null,viewRevision=0,densityRevision=0;
 const notice=document.getElementById('graphic-notice'),ackButton=document.getElementById('graphic-notice-ok');
 let noticePending=false,noticeDismissed=false;
 function maybeNotice(){
  if(noticePending&&!noticeDismissed&&current?.online&&current.s.terms_accepted&&current.s.telemetry_decided!==false&&!document.querySelector('dialog[open]'))notice.showModal();
 }
 function update(s,online){
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
  write('[data-car-soc]',number(m.carSoc,0)+' %');
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
  const vehicle=s.vehicle_status_enabled?(document.getElementById('vehicle-state')?.textContent||''):'';
  const note=!m.measured?'Aktuelle Wallbox-Messung fehlt':vehicle||text('power-note');
  write('[data-car-note]',note==='Gemessene Wirkleistung'?'':note);
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
  view=view==='graphic'?'graphic':'standard';
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
 window.TeeNetEnergy={update,setView,setDensity,loadDensity(density){if(!densityRevision)setDensity(density);},loadView(view){if(!viewRevision)setView(view);},loadNotice(pending){noticePending=pending;maybeNotice();}};
 window.addEventListener('teenet-status',maybeNotice);
 notice.addEventListener('cancel',()=>{noticeDismissed=true;});
 ackButton.addEventListener('click',async()=>{
  ackButton.disabled=true;document.getElementById('graphic-notice-status').textContent='';
  try{await api('/api/theme',{graphic_notice_ack:true},5000);noticePending=false;notice.close();}
  catch{document.getElementById('graphic-notice-status').textContent='Bestätigung konnte nicht gespeichert werden. Bitte erneut versuchen.';}
  finally{ackButton.disabled=false;}
 });
 for(const option of ['standard','graphic'])document.getElementById('view-'+option)?.addEventListener('click',()=>selectView(option));
 for(const option of ['standard','compact'])document.getElementById('density-'+option)?.addEventListener('click',()=>selectDensity(option));
 let saved,savedDensity;try{saved=localStorage.getItem('teennet-view');savedDensity=localStorage.getItem('teennet-density');}catch{}setView(saved);setDensity(savedDensity);
 document.addEventListener('visibilitychange',()=>{root.dataset.paused=String(document.hidden||!current?.online);});
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
 const icon='<svg class="en-art en-heatpump-art" viewBox="0 0 160 125" aria-hidden="true"><ellipse cx="80" cy="115" rx="55" ry="4" fill="var(--en-shadow)"/TeeNet/><path d="M111 73h17v29h-17" fill="none" stroke="var(--en-heatpump)" stroke-width="4" stroke-linejoin="round"/TeeNet/><rect x="25" y="34" width="91" height="72" rx="9" fill="var(--en-surface)" stroke="var(--en-heatpump)" stroke-width="3"/TeeNet/><path d="M37 106v7m67-7v7" stroke="var(--en-heatpump)" stroke-width="5" stroke-linecap="round"/TeeNet/><circle cx="64" cy="70" r="24" fill="var(--en-bg)" stroke="var(--en-line)" stroke-width="2"/TeeNet/><g class="en-heatpump-fan" fill="var(--en-heatpump)"><path d="M64 66c-4-16 4-21 12-16 7 5 2 15-8 20zM68 70c16-4 21 4 16 12-5 7-15 2-20-8zM64 74c4 16-4 21-12 16-7-5-2-15 8-20zM60 70c-16 4-21-4-16-12 5-7 15-2 20 8z"/TeeNet/><circle cx="64" cy="70" r="4" fill="var(--en-surface)"/TeeNet/></g><path d="M98 48h7m-7 8h7m-7 8h7m-7 8h7m-7 8h7m-7 8h7" stroke="var(--en-line)" stroke-width="2" stroke-linecap="round"/TeeNet/><path d="M44 25q-6-6 0-12m17 12q-6-6 0-12m17 12q-6-6 0-12" fill="none" stroke="var(--en-heatpump)" stroke-width="2.5" stroke-linecap="round"/TeeNet/></svg>';
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

/* Read-only consumer details, built from the same displayed live values. */
(()=>{
 'use strict';
 if(typeof document==='undefined')return;
 const root=document.getElementById('teenet-house-mood'),energy=window.TeeNetEnergy;
 if(!root||!energy||!document.body||energy.consumerDetailsInstalled)return;
 energy.consumerDetailsInstalled=true;
 const dialog=document.createElement('dialog');
 dialog.id='consumer-details';dialog.className='terms-dialog consumer-details';
 dialog.setAttribute('aria-labelledby','consumer-details-title');
 dialog.innerHTML='<h2 id="consumer-details-title"></h2><div class="consumer-detail-current"><span>Aktuelle Leistung</span><strong data-consumer-power></strong><small data-consumer-note></small></div><dl data-consumer-facts></dl><button type="button" class="button" id="consumer-details-close">Schließen</button>';
 document.body.append(dialog);
 let active=null;
 const text=(node,selector)=>node.querySelector(selector)?.textContent.trim()||'—';
 function update(){
  if(!active||!dialog.open)return;
  if(active.node.hidden||document.documentElement.dataset.dashboardView!=='graphic'||location.hash&&location.hash!=='#overview'){
   dialog.close();return;
  }
  const {name,node}=active;
  dialog.querySelector('#consumer-details-title').textContent=name;
  dialog.querySelector('[data-consumer-power]').textContent=text(node,'.en-power span')+' kW';
  dialog.querySelector('[data-consumer-note]').textContent=text(node,'.en-detail');
  const facts=dialog.querySelector('[data-consumer-facts]');
  const rows=[];
  if(node.classList.contains('en-wallbox-node')){
   if(!node.querySelector('[data-car-soc-block]').hidden)rows.push(['Auto-Akku',text(node,'[data-car-soc]')]);
   rows.push(['Heute geladen',text(node,'[data-energy-today]')],['Ladezeit heute',text(node,'[data-energy-time]')]);
  }
  // The house and heat pump have no daily energy meter here.
  // Do not invent kWh from a single instantaneous power reading.
  facts.replaceChildren(...rows.map(([label,value])=>{
   const row=document.createElement('div'),term=document.createElement('dt'),definition=document.createElement('dd');
   term.textContent=label;definition.textContent=value;row.append(term,definition);return row;
  }));
  facts.hidden=!rows.length;
 }
 function registerConsumers(){
 for(const [selector,name] of [['.en-home-node','Hausverbrauch'],['#heatpump-energy-node','Wärmepumpe'],['.en-wallbox-node','Wallbox']]){
  const node=root.querySelector(selector),art=node?.querySelector('.en-art');
  if(!art||node.classList.contains('en-clickable'))continue;
  const button=document.createElement('button');button.type='button';button.className='en-picture-button';
  button.setAttribute('aria-label',name+': Werte anzeigen');button.setAttribute('aria-haspopup','dialog');button.setAttribute('aria-controls',dialog.id);
  button.title=name+' · Werte anzeigen';
  node.insertBefore(button,art);button.append(art);node.classList.add('en-clickable');
  button.addEventListener('click',()=>{active={node,name,button};dialog.showModal();update();});
 }
 }
 registerConsumers();
 dialog.querySelector('#consumer-details-close').addEventListener('click',()=>dialog.close());
 dialog.addEventListener('close',()=>{const button=active?.button;active=null;if(button?.getClientRects().length)button.focus({preventScroll:true});});
 window.addEventListener('hashchange',update);
 const previous=energy.update;
 energy.update=function(s,online){previous.call(this,s,online);registerConsumers();update();};
 new MutationObserver(update).observe(document.documentElement,{attributes:true,attributeFilter:['data-dashboard-view']});
})();
