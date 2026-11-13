/* Synthetic direct percentage/manual planner; every request stays in the demo. */
(()=>{
 const previous=window.fetch.bind(window),key='teenet-demo-direct-plan-v1';
 let goal={kind:'kwh',mode:'pv'};try{goal=JSON.parse(localStorage.getItem(key)||'null')||goal;}catch{}
 const valid=n=>Number.isFinite(n)&&n>=0&&n<=100;
 const json=(data,status=200)=>new Response(JSON.stringify(data),{status,headers:{'Content-Type':'application/json'}});
 const remember=()=>{try{localStorage.setItem(key,JSON.stringify(goal));}catch{}};
 const measured=s=>window.teenetDemoCarSoc!==undefined?window.teenetDemoCarSoc:s.car_soc_pct;
 window.fetch=async(input,options={})=>{
  const url=new URL(typeof input==='string'?input:input.url,location.origin);
  if(url.origin!==location.origin)return previous(input,options);
  if(url.pathname==='/api/status'&&options.method!=='POST'){
   const response=await previous(input,options);if(!response.ok)return response;const s=await response.json(),p=s.charge_plan||{};
   s.car_soc_pct=measured(s);
   if(goal.kind==='soc'){
    const soc=valid(s.car_soc_pct)?s.car_soc_pct:Math.min(100,goal.start+(p.delivered_kwh||0)*.9/goal.capacity*100);
    if(p.active&&soc>=goal.target){await previous('/api/plan',{method:'POST',body:JSON.stringify({active:false})});p.active=false;p.phase='complete';p.delivered_kwh=p.target_kwh;goal.complete=true;remember();}
    Object.assign(p,{kind:'soc',mode:goal.mode,capacity_kwh:goal.capacity,target_soc:goal.target,estimated_soc:soc});
    if(goal.complete&&!p.active)p.phase='complete';
   }else Object.assign(p,{kind:goal.kind,mode:goal.mode});
   return json(s);
  }
  if(url.pathname==='/api/plan'&&options.method==='POST'){
   let body;try{body=JSON.parse(options.body||'{}');}catch{return json({error:'Ungültige Plandaten.'},400);}
   if(body.active&&!body.resume&&(!Number.isInteger(body.deadline_epoch)||body.deadline_epoch<=Date.now()/1000||body.deadline_epoch>Date.now()/1000+7*86400))return json({error:'Endzeit in der Zukunft innerhalb der nächsten 7 Tage wählen.'},400);
   if(body.active&&!body.resume&&(body.capacity_kwh!==undefined||body.current_soc!==undefined||body.target_soc!==undefined)){
    const status=await (await previous('/api/status')).json(),current=valid(measured(status))?measured(status):body.current_soc;
    if(!Number.isFinite(body.capacity_kwh)||body.capacity_kwh<5||body.capacity_kwh>200)return json({error:'Akkugröße zwischen 5 und 200 kWh eingeben.'},400);
    if(!valid(current)||!valid(body.target_soc)||body.target_soc<=current)return json({error:'Gültigen Ladestand und höheres Ladeziel eingeben.'},400);
    const kwh=body.capacity_kwh*(body.target_soc-current)/100/.9;
    const result=await previous(input,{...options,body:JSON.stringify({active:true,kwh,deadline_epoch:body.deadline_epoch,mode:body.mode})});
    if(result.ok){goal={kind:'soc',mode:body.mode==='manual'?'manual':'pv',capacity:body.capacity_kwh,start:current,target:body.target_soc};remember();}
    return result;
   }
   const result=await previous(input,options);
   if(result.ok&&!body.resume){goal={kind:'kwh',mode:body.active&&body.mode==='manual'?'manual':'pv'};remember();}return result;
  }
  if(url.pathname==='/api/vehicles/plan'&&options.method==='POST'){
   const result=await previous(input,options);if(result.ok){const body=JSON.parse(options.body||'{}');goal={kind:'vehicle',mode:body.mode==='manual'?'manual':'pv'};remember();}return result;
  }
  return previous(input,options);
 };
})();
