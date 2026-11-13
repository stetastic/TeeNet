/* Local/shareable simulation only. All intercepted routes stay on this page's origin. */
(() => {
 const previous=window.fetch.bind(window),empty=()=>({name:'',plate:'',cards:['',''],capacity_kwh:0,maximum_kw:0,efficiency:0,target_soc:0,soc_source:0});
 const json=(v,status=200)=>new Response(JSON.stringify(v),{status,headers:{'Content-Type':'application/json'}});
 let store={enabled:false,revision:1,profiles:Array.from({length:4},empty)},plan={active:false,slot:0,reason:'',target_soc:0},photos={},hasSavedStore=false;
 let manualEstimate=null;
 const assetKey='teenet-demo-vehicle-assets-v1';
 try{const bundle=JSON.parse(localStorage.getItem(assetKey)||'null'),saved=bundle?.store||JSON.parse(localStorage.getItem('teenet-demo-vehicles'));if(saved?.profiles?.length===4){store=saved;hasSavedStore=true;}photos=bundle?.photos||JSON.parse(localStorage.getItem('teenet-demo-vehicle-photos')||'{}');}catch{}
 if(window.teenetDemoShowcase&&!hasSavedStore){store.enabled=true;store.profiles[0]={name:'Mercedes GLB 250+',plate:'',cards:['DEMO-CARD-1',''],capacity_kwh:85,maximum_kw:11,efficiency:.9,target_soc:80,soc_source:2,picture:3};try{persist();}catch{}}
 // Update the old stock showcase once; retain customized vehicles and uploaded photos.
 if(window.teenetDemoShowcase&&!window.teenetLocalDemoFeatures){
  try{
   const migration='teenet-demo-glb-start-v1';
   if(localStorage.getItem(migration)!=='1'){
    const p=store.profiles[0];
    const stock=p?.name==='Demo-Fahrzeug'&&p.plate===''&&p.picture===1&&p.cards?.length===2&&p.cards[0]==='DEMO-CARD-1'&&p.cards[1]===''&&p.capacity_kwh===77&&p.maximum_kw===11&&p.efficiency===.9&&p.target_soc===80&&p.soc_source===2&&!photos[1];
    if(stock){store.profiles[0]={...p,name:'Mercedes GLB 250+',capacity_kwh:85,picture:3};store.revision=(Number(store.revision)||1)+1;persist();}
    localStorage.setItem(migration,'1');
   }
  }catch{}
 }
 // Adopt the requested local start profile once; retain all saved cars, cards and photos.
 if(window.teenetLocalDemoFeatures?.firstRun){store.enabled=true;try{persist();}catch{}}
 function persist(next=store,nextPhotos=photos){localStorage.setItem(assetKey,JSON.stringify({store:next,photos:nextPhotos}));store=next;photos=nextPhotos;}
 const profileKeys=['name','plate','cards','capacity_kwh','maximum_kw','efficiency','target_soc','soc_source','picture'];
 function validProfile(p){
  const text=(v,n)=>{try{return typeof v==='string'&&!/[\x00-\x1f]/.test(v)&&encodeURIComponent(v).replace(/%[A-F\d]{2}|./g,'x').length<n;}catch{return false;}};
  if(!p||Object.keys(p).some(k=>!profileKeys.includes(k))||!text(p.name,49)||!text(p.plate,25)||!Array.isArray(p.cards)||p.cards.length!==2||p.cards.some(c=>c!==''&&!(typeof c==='string'&&/^[\x21-\x7e]{1,48}$/.test(c)))||!Number.isInteger(p.soc_source)||p.soc_source<0||p.soc_source>2||!Number.isInteger(p.picture??0)||(p.picture??0)<0||(p.picture??0)>5)return false;
  if(!p.name)return !p.plate&&!p.cards.some(Boolean)&&!p.soc_source&&!(p.picture??0)&&['capacity_kwh','maximum_kw','efficiency','target_soc'].every(k=>p[k]===0);
  return [['capacity_kwh',5,200],['maximum_kw',1.4,22],['efficiency',.5,1],['target_soc',1,100]].every(([k,min,max])=>Number.isFinite(p[k])&&p[k]>=min&&p[k]<=max);
 }
 function validStore(rows){const cards=rows.flatMap(p=>p.cards).filter(Boolean);return rows.length===4&&rows.every(validProfile)&&new Set(cards).size===cards.length&&rows.filter(p=>p.name&&p.soc_source===2).length<=1;}
 function photoValid(value){try{const p=atob(value);return typeof value==='string'&&value.length<=2048&&(!value||(p.length>=4&&p.charCodeAt(0)===255&&p.charCodeAt(1)===216&&p.charCodeAt(p.length-2)===255&&p.charCodeAt(p.length-1)===217));}catch{return false;}}
 async function snapshot(){
  const r=await previous('/api/status'),s=await r.json(),config=await (await previous('/api/config')).json();
  const profiles=store.profiles.map((p,i)=>({...p,soc:p.soc_source===2&&config.vehicle_soc_enabled&&config.mqtt_enabled&&Number.isFinite(s.car_soc_pct)?s.car_soc_pct:null,soc_estimated:false,soc_topic:`wallbox-ems/input/vehicle_${i+1}_soc_pct`}));
  const fixed=config.vehicle_fixed_profile||0;
  const matched=store.enabled?(fixed?(profiles[fixed-1]?.name?fixed-1:-1):profiles.findIndex(p=>p.cards.some(c=>c==='DEMO-CARD-1'))):-1;
  const total=Number(s.energy?.[0]?.total_kwh);
  if(manualEstimate){
   const e=manualEstimate,p=profiles[e.slot-1];
   if(!p||matched+1!==e.slot||store.revision!==e.revision||fixed!==e.fixed||!Number.isFinite(total)||total<e.anchor)manualEstimate=null;
   else if(Number.isFinite(p.soc)){if(Math.abs(p.soc-e.start)>=.1){e.start=p.soc;e.anchor=total;}}
   else{p.soc=Math.min(100,e.start+(total-e.anchor)*p.efficiency/p.capacity_kwh*100);p.soc_estimated=true;}
  }
  const maximum=Math.min(11.04,plan.active?profiles[plan.slot-1].maximum_kw:11.04),remaining=Math.max(0,(s.charge_plan?.target_kwh||0)-(s.charge_plan?.delivered_kwh||0));
  if(plan.active&&!s.charge_plan?.active)plan.active=false;
  const measured=plan.active&&Number.isFinite(profiles[plan.slot-1].soc)&&!profiles[plan.slot-1].soc_estimated;
  return {...store,fixed_profile:fixed,manual_soc_prompt:config.vehicle_manual_soc_prompt!==false,profiles,recognized_slot:matched+1,detected_card:'DEMO-CARD-1',wallbox_fresh:true,wallbox_session_kwh:1.2,plan:{...plan,maximum_kw:maximum,measured_soc:measured,estimated_soc:plan.active?measured?profiles[plan.slot-1].soc:Math.min(100,plan.start_soc+(s.charge_plan?.delivered_kwh||0)*profiles[plan.slot-1].efficiency/profiles[plan.slot-1].capacity_kwh*100):null,grid_start_epoch:plan.active?Math.floor(s.charge_plan.deadline_epoch-remaining/maximum*3600-Math.max(300,remaining/maximum*360)):0,earliest_finish_epoch:plan.active?Date.now()/1000+remaining/maximum*3600:null,deadline_feasible:plan.active?Date.now()/1000+remaining/maximum*3600<=s.charge_plan.deadline_epoch:false}};
 }
 window.fetch=async(input,options={})=>{
  const url=new URL(typeof input==='string'?input:input.url,location.origin);
  if(url.origin===location.origin&&url.pathname==='/api/config'&&options.method==='POST'){
   let body;try{body=JSON.parse(options.body||'{}');}catch{return json({error:'Ungültiges JSON.'},400);}
   if(body.vehicle_fixed_profile!==undefined){
    const fixed=body.vehicle_fixed_profile,old=(await (await previous('/api/config')).json()).vehicle_fixed_profile||0;
    if(!Number.isInteger(fixed)||fixed<0||fixed>4||(fixed&&!store.profiles[fixed-1]?.name))return json({error:'Zuerst das gewünschte Fahrzeugprofil speichern.',field:'vehicle_fixed_profile'},400);
    if(fixed!==old&&plan.active)return json({error:'Fahrzeug-Ladeplan vor dem Profilwechsel beenden.',field:'vehicle_fixed_profile'},400);
   }
  }
  if(url.origin===location.origin&&url.pathname==='/api/backup' &&options.method!=='POST'){
   const response=await previous(input,options);if(!response.ok)return response;const backup=await response.json();
   return json({...backup,schema:2,vehicles:{version:1,enabled:store.enabled,profiles:store.profiles,photos:Array.from({length:4},(_,i)=>photos[i+1]||'')}});
  }
  if(url.origin===location.origin&&url.pathname==='/api/restore'&&options.method==='POST'){
   let backup;try{backup=JSON.parse(options.body||'{}');}catch{return json({error:'Ungültiges JSON.'},400);}
   let nextStore=store,nextPhotos=photos;
   if(backup?.schema===2){const a=backup.vehicles;if(a?.version!==1||typeof a.enabled!=='boolean'||!Array.isArray(a.profiles)||!a.profiles.every(p=>p&&Array.isArray(p.cards))||!validStore(a.profiles)||!Array.isArray(a.photos)||a.photos.length!==4||!a.photos.every(photoValid))return json({error:'Fahrzeugdaten oder Bilder ungültig.'},400);nextStore={enabled:a.enabled,revision:store.revision+1,profiles:a.profiles};nextPhotos=Object.fromEntries(a.photos.map((p,i)=>[i+1,p]));}
   if(backup?.format!=='TeeNet-settings'||![1,2].includes(backup.schema)||!backup.settings||typeof backup.settings!=='object'||Array.isArray(backup.settings))return json({error:'Keine passende Sicherung.'},400);
   const oldStore=store,oldPhotos=photos;try{persist(nextStore,nextPhotos);}catch{return json({error:'Fahrzeugdaten nicht gespeichert.'},500);}
   const payload={...backup,schema:1};delete payload.vehicles;
   const response=await previous(input,{...options,body:JSON.stringify(payload)});if(!response.ok){try{persist(oldStore,oldPhotos);}catch{}}else plan={active:false,slot:0,reason:'',target_soc:0};return response;
  }
  if(url.origin===location.origin&&url.pathname==='/api/plan'&&options.method==='POST'){
   let request;try{request=JSON.parse(options.body||'{}');}catch{return json({error:'Ungültiges JSON.'},400);}if(!request||typeof request!=='object'||Array.isArray(request))return json({error:'Ungültiges JSON-Objekt.'},400);
   if(request.active&&request.resume&&plan.active)return json({error:'Fahrzeug-Ladeplan über das zugeordnete Profil fortsetzen.'},400);
   const response=await previous(input,options);if(response.ok&&(!request.active||!request.resume))plan={active:false,slot:0,reason:'',target_soc:0};return response;
  }
  if(url.origin!==location.origin||!url.pathname.startsWith('/api/vehicles'))return previous(input,options);
  let body={};try{body=JSON.parse(options.body||'{}');}catch{return json({error:'Ungültiges JSON.'},400);}if(!body||typeof body!=='object'||Array.isArray(body))return json({error:'Ungültiges JSON-Objekt.'},400);
  if(options.method==='POST'){
   const s=await (await previous('/api/status')).json();if(!s.terms_accepted)return json({error:'Nutzungsbedingungen zuerst bestätigen.'},403);
  }
  if(url.pathname==='/api/vehicles/estimate'&&options.method==='POST'){
   const s=await snapshot(),p=s.profiles[body.slot-1],status=await (await previous('/api/status')).json(),total=Number(status.energy?.[0]?.total_kwh);
   if(Object.keys(body).length!==2||!Number.isInteger(body.slot)||body.slot<1||body.slot>4||s.recognized_slot!==body.slot||!p?.name||!Number.isFinite(body.start_soc)||body.start_soc<0||body.start_soc>100||!Number.isFinite(total))return json({error:'Erkanntes Fahrzeug und Startwert zwischen 0 und 100 % erforderlich.'},400);
   if(!Number.isFinite(p.soc)||p.soc_estimated)manualEstimate={slot:body.slot,start:body.start_soc,anchor:total,revision:store.revision,fixed:s.fixed_profile};
   return json(await snapshot());
  }
  if(url.pathname==='/api/vehicles'){
   if(options.method==='POST'){
    const p=body.profile,slot=body.slot;
    if(body.revision!==store.revision)return json({error:'Profile inzwischen geändert.'},409);
    if(Object.keys(body).length===2&&typeof body.enabled==='boolean'&&!p&&slot===undefined){
     if(plan.active)return json({error:'Ladeplan zuerst beenden.'},409);
     try{persist({...store,enabled:body.enabled,revision:store.revision+1});}catch{return json({error:'Nicht gespeichert.'},500);}return json(await snapshot());
    }
    if(!Number.isInteger(slot)||slot<1||slot>4||!p||!Array.isArray(p.cards)||p.cards.length!==2||typeof body.enabled!=='boolean')return json({error:'Profil unvollständig.'},400);
    if(p.picture!==undefined&&(!Number.isInteger(p.picture)||p.picture<0||p.picture>5))return json({error:'Fahrzeugbild auswählen.'},400);
    const candidate=structuredClone(store);candidate.enabled=body.enabled;candidate.profiles[slot-1]={...p,cards:p.cards.map(c=>String(c).trim().toUpperCase())};
    const fixed=(await (await previous('/api/config')).json()).vehicle_fixed_profile||0;
    if(fixed===slot&&!p.name)return json({error:'Vor dem Löschen ein anderes festes Profil oder Ladekartenerkennung wählen und speichern.'},409);
    if(!validStore(candidate.profiles))return json({error:'Profil, Ladekarten oder SoC-Quelle prüfen.'},400);
    if(plan.active)return json({error:'Ladeplan zuerst beenden.'},409);
    candidate.revision++;try{persist(candidate);}catch{return json({error:'Nicht gespeichert.'},500);}
   }
   return json(await snapshot());
  }
  if(url.pathname==='/api/vehicles/photo'){
   const slot=Number(url.searchParams.get('slot'));if(!Number.isInteger(slot)||slot<1||slot>4)return json({error:'Fahrzeug fehlt.'},400);
   if(options.method==='POST'){if(!photoValid(body.jpeg_base64))return json({error:'JPEG-Bild bis 1536 Bytes auswählen.'},400);try{persist(store,{...photos,[slot]:body.jpeg_base64});}catch{return json({error:'Bild nicht gespeichert.'},500);}return json({ok:true});}
   if(!photos[slot])return json({error:'Kein Bild.'},404);
   return new Response(Uint8Array.from(atob(photos[slot]),c=>c.charCodeAt(0)),{headers:{'Content-Type':'image/jpeg'}});
  }
  if(url.pathname==='/api/vehicles/plan'&&options.method==='POST'){
   const p=store.profiles[body.slot-1],s=await snapshot(),active=s.profiles[body.slot-1],soc=Number.isFinite(active?.soc)&&!active.soc_estimated?active.soc:body.current_soc,now=Math.floor(Date.now()/1000);
   if(!store.enabled||!p?.name||!Number.isInteger(body.slot)||s.recognized_slot!==body.slot||!Number.isFinite(body.current_soc)||!Number.isFinite(soc)||soc<0||soc>100||!Number.isFinite(body.target_soc)||body.target_soc<=soc||body.target_soc>100||!Number.isInteger(body.deadline_epoch)||body.deadline_epoch<=now||body.deadline_epoch>now+7*86400||(body.resume!==undefined&&typeof body.resume!=='boolean')||(body.resume&&(!plan.active||plan.slot!==body.slot)))return json({error:'Fahrzeug, Karte, Ladeziel und Endzeit prüfen.'},400);
   const kwh=p.capacity_kwh*(body.target_soc-soc)/100/p.efficiency;if(!Number.isFinite(kwh)||kwh<=0||kwh>400)return json({error:'Fahrzeugparameter prüfen.'},400);
   const result=await previous('/api/plan',{method:'POST',headers:options.headers,body:JSON.stringify(body.resume?{active:true,resume:true,mode:body.mode}:{active:true,kwh,deadline_epoch:body.deadline_epoch,mode:body.mode})});
   if(!result.ok)return result;
   if(!body.resume)plan={active:true,slot:body.slot,reason:'',start_soc:soc,target_soc:body.target_soc};return json(await snapshot());
  }
  return json({error:'Unbekannte Profilfunktion.'},404);
 };
})();
