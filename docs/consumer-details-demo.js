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
