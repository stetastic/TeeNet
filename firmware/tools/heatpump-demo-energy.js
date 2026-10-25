/* Optional heat-pump display for the isolated demo. No device control. */
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
  return {enabled,power,houseLoad,flow:power===null?'unknown':power>50?'forward':'idle'};
 }
 if(typeof module==='object'&&module.exports)module.exports={model};
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
  const showHome=m.houseLoad!==null;
  root.querySelector('.en-home-node').hidden=!showHome;
  paths.querySelector('[data-route="home"]').toggleAttribute('hidden',!showHome);
  paths.querySelector('.en-home-junction').toggleAttribute('hidden',!showHome);
  root.querySelector('[data-house-load]').textContent=power(m.houseLoad);
  root.querySelector('[data-house-note]').textContent='ohne Wallbox und Wärmepumpe';
  consumers.style.setProperty('--en-consumer-count',showHome?'3':'2');
  const hpX=showHome?300:150,carX=showHome?500:450;
  if(showHome)consumerRoute('home',100,m.houseLoad>50?'forward':'idle');
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
