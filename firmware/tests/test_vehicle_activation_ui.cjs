const assert=require('node:assert/strict'),fs=require('node:fs'),vm=require('node:vm');
const html=fs.readFileSync('main/dashboard.html','utf8'),source=fs.readFileSync('main/features.js','utf8').split('/* VEHICLE_UI_BEGIN */')[1].split('/* VEHICLE_UI_END */')[0];
class Element {
 constructor(id='',tag='DIV'){Object.assign(this,{id,tagName:tag,children:[],dataset:{},attributes:{},events:{},value:'',checked:false,hidden:false,disabled:false,open:false,textContent:''});}
 addEventListener(type,fn){(this.events[type]??=[]).push(fn);}
 set value(value){this._value=String(value);}
 get value(){return this._value;}
 emit(type){for(const fn of this.events[type]||[])fn({target:this,preventDefault(){},stopPropagation(){}});}
 setAttribute(k,v){this.attributes[k]=String(v);}
 getAttribute(k){return this.attributes[k];}
 append(...children){this.children.push(...children);}
 replaceChildren(...children){this.children=children;}
 querySelectorAll(selector){return this.children.filter(c=>selector==='button'&&c.tagName==='BUTTON');}
 cloneNode(){const e=new Element(this.id,this.tagName);e.value=this.value;e.textContent=this.textContent;return e;}
 showModal(){this.open=true;}close(){this.open=false;}
 reportValidity(){throw Error('Feature activation must not validate an empty vehicle editor');}
}
const elements=Object.fromEntries([...html.matchAll(/\bid="([^"]+)"/g)].map(m=>[m[1],new Element(m[1])]));
elements['config-form'].elements={basic_mode:{value:'false'}};
elements['vehicle-edit-slot'].value='1';elements['plan-kind'].value='kwh';elements['vehicle-plan-profile'].value='';elements['plan-deadline'].value=new Date(Date.now()+86400000).toISOString().slice(0,16);elements['vehicle-target-soc'].value='80';elements['settings'].hidden=false;elements['overview'].hidden=true;
const empty=()=>({name:'',plate:'',cards:['',''],capacity_kwh:0,maximum_kw:0,efficiency:0,target_soc:0,soc_source:0,picture:0,soc:null});
let backend={enabled:false,revision:1,profiles:Array.from({length:4},empty),recognized_slot:0,wallbox_fresh:true,detected_card:'DEMO',plan:{active:false,maximum_kw:11}},saves=0,unsaved=0,lastPlanRequest=null,now=Date.now(),failRead=false;
class Clock extends Date {static now(){return now;}}
const events={};const window={TeeNetVehiclePictures:require('../main/vehicle-pictures.js'),addEventListener(n,fn){(events[n]??=[]).push(fn);},dispatchEvent(e){for(const fn of events[e.type]||[])fn(e);}};
const context={window,document:{hidden:false,getElementById:id=>elements[id],createElement:tag=>new Element('',tag.toUpperCase())},CustomEvent:class{constructor(type){this.type=type;}},URL,Date:Clock,Number,Math,online:true,busy:false,state:{basic_mode:false,charge_plan:{}},fetch:async()=>new Response('',{status:404}),updateUnsavedFunctions(){unsaved++;},toast(){},action:fn=>fn(),pollStatus:async()=>{},setTimeout,
 api:async(path,body)=>{if(path==='/api/plan'){lastPlanRequest=body;return {ok:true};}if(path==='/api/vehicles/plan'){lastPlanRequest=body;backend.plan={active:true,slot:body.slot,target_soc:body.target_soc,maximum_kw:11,estimated_soc:body.current_soc};return structuredClone(backend);}if(path==='/api/vehicles/estimate'){assert.deepEqual(Object.keys(body).sort(),['slot','start_soc']);backend.profiles[body.slot-1].soc=body.start_soc;backend.profiles[body.slot-1].soc_estimated=true;return structuredClone(backend);}assert.equal(path,'/api/vehicles');if(!body&&failRead)throw Error('Connection lost');if(body){assert.deepEqual(Object.keys(body).sort(),['enabled','revision']);assert.equal(body.revision,backend.revision);backend.enabled=body.enabled;backend.revision++;saves++;}return structuredClone(backend);}};
vm.runInNewContext(source,context);
const flush=()=>new Promise(resolve=>setImmediate(resolve));
(async()=>{
 window.dispatchEvent(new context.CustomEvent('teenet-status'));await flush();
 assert.equal(elements['vehicle-profiles-settings'].hidden,true,'disabled feature must stay hidden');assert.equal(elements['vehicle-plan-selector'].hidden,false,'kWh/percent choice stays visible without profiles');assert.equal(elements['plan-kind-vehicle'].disabled,false,'percent entry remains available without a vehicle profile');
 const toggle=elements['vehicle-enabled'];toggle.checked=true;toggle.emit('change');
 assert.equal(elements['vehicle-profiles-settings'].hidden,false);assert.equal(elements['vehicle-profiles-settings'].open,true);assert(window.teenetVehicles.isDirty());
 await window.teenetVehicles.saveFeature();assert.equal(saves,1);assert.equal(backend.enabled,true);assert.equal(window.teenetVehicles.isDirty(),false);
 toggle.checked=false;toggle.emit('change');assert.equal(elements['vehicle-profiles-settings'].hidden,true);await window.teenetVehicles.saveFeature();assert.equal(backend.enabled,false);assert.equal(saves,2);
 elements['config-form'].elements.basic_mode.value='true';window.teenetVehicles.syncFeature();assert.equal(elements['vehicle-profiles-settings'].hidden,true);
 assert(unsaved>=2);assert.equal(elements['vehicle-picture-options'].children.length,5);
 assert(html.indexOf('id="vehicle-profiles-settings"')<html.indexOf('id="maintenance"'));
 const card=html.slice(html.indexOf('id="vehicle-profiles-settings"'),html.indexOf('id="maintenance"'));assert(!card.includes('<form'),'no nested form in configuration');
 assert(html.includes('id="vehicle-enabled" form="vehicle-profile-form"'));
 assert(html.indexOf('id="vehicle-settings"')<html.indexOf('id="vehicle-profiles-settings"'),'profiles belong inside the common vehicle section');
 assert(!html.includes('Freies Profil'));assert(!html.includes('id="vehicle-capacity" type="number" min="5" max="200" step="0.1" value="60"'));
 elements['config-form'].elements.basic_mode.value='false';backend.enabled=true;backend.recognized_slot=1;
 backend.profiles[0]={...empty(),name:'Car A',plate:'A 1',cards:['A',''],capacity_kwh:60,maximum_kw:11,efficiency:.9,target_soc:80,picture:3,soc:64};
 backend.profiles[1]={...empty(),name:'Car B',plate:'B 2',cards:['B',''],capacity_kwh:40,maximum_kw:11,efficiency:.9,target_soc:80,picture:1};
 backend.revision++;
 const refresh=async()=>{now+=5001;window.dispatchEvent(new context.CustomEvent('teenet-status'));await flush();};
 await refresh();assert.equal(elements['vehicle-active-soc'].hidden,false);assert.equal(elements['vehicle-active-soc-value'].textContent,'64 %');assert.equal(elements['vehicle-active-soc-fill'].attributes.width,'32');assert.equal(elements['plan-kind-vehicle'].disabled,false);assert.equal(elements['plan-kind'].value,'vehicle','a known SOC offers the percent goal');
 assert.equal(elements['vehicle-active-card'].textContent,'Aktive Ladekarte: DEMO');assert.equal(elements['vehicle-active-card'].hidden,false);
 backend.wallbox_fresh=false;await refresh();assert.equal(elements['vehicle-active-card'].hidden,true,'Never show a stale card as active');
 backend.wallbox_fresh=true;backend.detected_card='';await refresh();assert.equal(elements['vehicle-active-card'].hidden,true,'Missing card is hidden');
 backend.detected_card='DEMO';await refresh();assert.equal(elements['vehicle-active-card'].hidden,false);
 backend.profiles[0].soc=null;await refresh();assert.equal(elements['vehicle-active-soc'].hidden,true);assert.equal(elements['vehicle-current-soc'].value,'','stale MQTT SOC must not become a manual start value');
 elements['vehicle-current-soc'].value='25';backend.recognized_slot=2;await refresh();assert.equal(elements['vehicle-current-soc'].value,'','manual SOC belongs to its car');
 elements['vehicle-current-soc'].value='35';await refresh();assert.equal(elements['vehicle-current-soc'].value,'35','same-car polling preserves manual input');
 backend.plan={active:true,slot:2,maximum_kw:11,estimated_soc:null,target_soc:80};await refresh();assert(elements['vehicle-plan-live'].textContent.includes('— %'),'unknown planned SOC must not become 0%');backend.plan={active:false,maximum_kw:11};await refresh();assert.equal(elements['vehicle-current-soc'].value,'','completed plan must not reuse its old manual start SOC');
 backend.recognized_slot=1;backend.profiles[0].soc=0;await refresh();assert.equal(elements['vehicle-active-soc'].hidden,false);assert.equal(elements['vehicle-active-soc-value'].textContent,'0 %');
 backend.profiles[0].soc=101;await refresh();assert.equal(elements['vehicle-active-soc'].hidden,true);
 backend.profiles[0].soc=64;await refresh();failRead=true;await refresh();assert.equal(elements['vehicle-active-profile'].hidden,true);assert.equal(window.teenetVehicles.energyStatus().active,false);
 failRead=false;await refresh();assert.equal(elements['vehicle-active-soc'].hidden,false);
 context.busy=true;now+=16000;window.dispatchEvent(new context.CustomEvent('teenet-status'));assert.equal(elements['vehicle-active-soc'].hidden,true);context.busy=false;await refresh();
 backend.recognized_slot=0;await refresh();assert.equal(elements['vehicle-active-profile'].hidden,true);assert.equal(window.teenetVehicles.energyStatus().active,false);
 backend.recognized_slot=1;const good=backend;backend={...backend,profiles:[null,...backend.profiles.slice(1)]};await refresh();assert.equal(elements['vehicle-active-profile'].hidden,true);backend=good;await refresh();
 let finishImage,closed=0;context.createImageBitmap=()=>new Promise(resolve=>{finishImage=resolve;});
 elements['vehicle-photo'].files=[{size:1024,type:'image/jpeg'}];elements['vehicle-photo'].emit('change');
 elements['vehicle-picture-options'].children.find(button=>button.dataset.picture==='3').emit('click');
 finishImage({close(){closed++;}});await flush();assert.equal(closed,1);assert.equal(elements['vehicle-picture'].value,'3');assert.equal(elements['vehicle-photo-preview'].src,window.TeeNetVehiclePictures.source(3),'late upload must not overwrite a newer preset selection');
 assert.equal(elements['vehicle-edit-slot'].children[0].textContent,'Car A','Saved vehicle names are the selector labels');
 elements['vehicle-name'].value='Familienauto';for(const fn of elements['vehicle-profile-fields'].events.input)fn({target:elements['vehicle-name'],stopPropagation(){}});
 assert.equal(elements['vehicle-edit-slot'].children[0].textContent,'Familienauto','typed name updates the selected entry');
 window.confirm=()=>true;elements['vehicle-edit-slot'].value='3';elements['vehicle-edit-slot'].emit('change');assert.equal(elements['vehicle-capacity'].value,'','empty vehicles have no invented battery size');
 elements['vehicle-picture-options'].children.find(button=>button.dataset.picture==='1').emit('click');elements['vehicle-battery-variant'].value='0';elements['vehicle-battery-variant'].emit('change');assert.equal(elements['vehicle-capacity'].value,'77');
 elements['vehicle-picture-options'].children.find(button=>button.dataset.picture==='2').emit('click');elements['vehicle-battery-variant'].value='1';elements['vehicle-battery-variant'].emit('change');assert.equal(elements['vehicle-capacity'].value,'19.7');
 elements['vehicle-picture-options'].children.find(button=>button.dataset.picture==='3').emit('click');assert.equal(elements['vehicle-capacity'].value,'19.7','image selection never overwrites a capacity');
 for(const picture of [1,2,3,4,5]){
  elements['vehicle-picture-options'].children.find(button=>button.dataset.picture===String(picture)).emit('click');
  assert(elements['vehicle-battery-variant'].children.length>1,'every picture has verified variants');
  assert.equal(elements['vehicle-capacity'].value,'19.7','picture changes preserve manual capacities');
 }
 backend.fixed_profile=1;backend.wallbox_fresh=false;backend.profiles[0].cards=['',''];backend.revision++;await refresh();
 assert.equal(elements['vehicle-active-profile'].hidden,false);assert.equal(elements['vehicle-active-card'].textContent,'Fest ausgewähltes Fahrzeug');assert(window.teenetVehicles.energyStatus().active);
 assert.equal(elements['vehicle-fixed-profile'].children[1].textContent,'Fest: Car A');assert.equal(elements['vehicle-fixed-profile'].children[3].disabled,true);
 backend.fixed_profile=0;backend.wallbox_fresh=true;await refresh();
 assert(window.teenetVehicles.showDetails());assert(elements['vehicle-details-dialog'].open);assert.equal(elements['vehicle-details-name'].textContent,'Car A');assert(elements['vehicle-details-facts'].children.length>=3);assert(!elements['vehicle-details-facts'].children.some(row=>['Ladewirkungsgrad','Ladezustandsquelle'].includes(row.children[0].textContent)));elements['vehicle-details-close'].emit('click');assert(!elements['vehicle-details-dialog'].open);
 backend.profiles[0].soc=null;backend.manual_soc_prompt=true;await refresh();
 let starting=window.teenetVehicles.beforeStart();await flush();assert(elements['vehicle-start-soc-dialog'].open);
 elements['vehicle-start-soc-cancel'].emit('click');assert.equal(await starting,false);
 starting=window.teenetVehicles.beforeStart();await flush();elements['vehicle-start-soc-skip'].emit('click');assert.equal(await starting,true);assert.equal(backend.profiles[0].soc,null);
 starting=window.teenetVehicles.beforeStart();await flush();elements['vehicle-start-soc'].value='45';elements['vehicle-start-soc-form'].emit('submit');assert.equal(await starting,true);assert.equal(backend.profiles[0].soc,45);assert(window.teenetVehicles.energyStatus().estimated);
 starting=window.teenetVehicles.beforeStart();await flush();assert(elements['vehicle-start-soc-dialog'].open);assert.equal(elements['vehicle-start-soc'].value,'45');elements['vehicle-start-soc-cancel'].emit('click');assert.equal(await starting,false);
 backend.profiles[0].soc=null;backend.manual_soc_prompt=false;assert.equal(await window.teenetVehicles.beforeStart(),true);assert(!elements['vehicle-start-soc-dialog'].open);
 backend.profiles[0].soc=45;backend.profiles[0].soc_estimated=true;await refresh();
 elements['plan-kind'].value='kwh';elements['plan-kind'].emit('change');assert(!elements['plan-kwh-label'].hidden);assert(elements['vehicle-plan-fields'].hidden);await refresh();assert.equal(elements['plan-kind'].value,'kwh','polling preserves explicit kWh choice');
 elements['plan-kind'].value='vehicle';elements['plan-kind'].emit('change');assert(!elements['vehicle-plan-fields'].hidden);assert(elements['plan-kwh-label'].hidden);assert.equal(elements['vehicle-current-soc'].value,'45','manual SOC is available to a percent plan');
 elements['vehicle-target-soc'].value='80';assert(window.teenetVehicles.usesVehicle());await window.teenetVehicles.startPlan();assert.equal(lastPlanRequest.current_soc,45);assert.equal(lastPlanRequest.target_soc,80);assert.equal(lastPlanRequest.slot,1);
 backend.enabled=false;backend.recognized_slot=0;backend.plan={active:false,maximum_kw:11};context.state={basic_mode:false,vehicle_soc_enabled:true,car_soc_pct:97,max_charge_a:16,power_per_amp_kw:.69,manual_power_kw:6,charge_plan:{}};await refresh();
 elements['mode'].value='manual';elements['plan-kind'].value='vehicle';elements['plan-kind'].emit('change');assert.equal(elements['plan-kind-vehicle'].disabled,false);assert.equal(elements['vehicle-current-soc'].value,'97');assert.equal(elements['plan-capacity-label'].hidden,false);assert.equal(elements['vehicle-plan-heading'].hidden,true);
 elements['plan-capacity'].value='60';elements['vehicle-target-soc'].value='100';await window.teenetVehicles.startPlan();assert.equal(lastPlanRequest.active,true);assert.equal(lastPlanRequest.mode,'manual');assert.equal(lastPlanRequest.current_soc,97);assert.equal(lastPlanRequest.target_soc,100);assert.equal(lastPlanRequest.capacity_kwh,60);
 context.online=false;window.dispatchEvent(new context.CustomEvent('teenet-status'));assert.equal(elements['vehicle-active-soc'].hidden,true);
 console.log('PASS: actual feature UI stays hidden until selected, ordinary save persists without editor validation, deselect hides, five picture presets and optional manual start SOC, valid form ownership and position before maintenance');
})().catch(e=>{console.error(e);process.exitCode=1;});
