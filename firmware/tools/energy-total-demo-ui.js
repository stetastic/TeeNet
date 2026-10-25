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
