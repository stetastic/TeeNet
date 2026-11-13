const fs=require('node:fs'),vm=require('node:vm'),assert=require('node:assert/strict');
let clock=Date.now();class DemoClock extends Date{static now(){return clock;}}
const memory=new Map(),location={origin:'https://demo.example',hostname:'demo.example',search:''};
memory.set('teenet-demo-terms','2026-10-09.2');
const window={location,dispatchEvent(){},fetch(){throw Error('Real network forbidden in demo test');}};
const context={window,location,URL,URLSearchParams,Response,Date:DemoClock,Math,Number,String,Map,JSON,structuredClone,CustomEvent:class{constructor(type,init){this.type=type;this.detail=init?.detail;}},document:{addEventListener(){}},localStorage:{getItem:k=>memory.get(k)||null,setItem:(k,v)=>memory.set(k,v)}};
for(const path of ['preview/demo-api.js','tools/evcc-demo-api.js','tools/heatpump-demo-api.js','tools/vehicle_demo_api.js'])vm.runInNewContext(fs.readFileSync(path,'utf8'),context);
(async()=>{
 const c=await (await window.fetch('/api/config')).json();
 assert.equal(c.basic_mode,false);assert.equal(c.heatpump_enabled,false);
 for(const k of ['zero_feed_enabled','pv_display_enabled','battery_protect','vehicle_soc_enabled','charge_plan_enabled'])assert.equal(c[k],true,k);
 const v=await (await window.fetch('/api/vehicles')).json();assert(v.enabled&&v.profiles[0].name==='Mercedes GLB 250+');assert(v.profiles[0].cards.includes('DEMO-CARD-1'));
 const html=fs.readFileSync('preview/index.html','utf8');
 assert(html.includes('<input name="heatpump_enabled" type="checkbox">Wärmepumpe anzeigen'));
 assert(!html.includes('<label class="check-label" hidden><input name="heatpump_enabled"'));
 for(const text of ['PV-Wechselrichter · Huawei SUN2000','PV-Wechselrichter · Sungrow'])assert(html.includes(text));
 const r=await window.fetch('/api/config',{method:'POST',body:JSON.stringify({heatpump_enabled:true})});assert(r.ok);
 assert.equal((await (await window.fetch('/api/config')).json()).heatpump_enabled,true,'the optional function can still be enabled');
 await window.fetch('/api/config',{method:'POST',body:JSON.stringify({vehicle_soc_enabled:false,vehicle_manual_soc_prompt:true})});
 let estimated=await (await window.fetch('/api/vehicles/estimate',{method:'POST',body:JSON.stringify({slot:1,start_soc:40})})).json();assert.equal(estimated.profiles[0].soc,40);assert(estimated.profiles[0].soc_estimated);
 clock+=60000;estimated=await (await window.fetch('/api/vehicles')).json();assert(estimated.profiles[0].soc>40&&estimated.profiles[0].soc<42);
 await window.fetch('/api/config',{method:'POST',body:JSON.stringify({vehicle_soc_enabled:true})});estimated=await (await window.fetch('/api/vehicles')).json();assert.equal(estimated.profiles[0].soc,64);assert(!estimated.profiles[0].soc_estimated);
 // Save a card-free vehicle, select it through the normal config route, and reload the shim.
 let cars=await (await window.fetch('/api/vehicles')).json();
 const own={...cars.profiles[0],name:'Festes Testfahrzeug',cards:['',''],soc_source:0};delete own.soc;delete own.soc_estimated;delete own.soc_topic;
 let response=await window.fetch('/api/vehicles',{method:'POST',body:JSON.stringify({slot:2,enabled:true,revision:cars.revision,profile:own})});assert(response.ok);
 response=await window.fetch('/api/config',{method:'POST',body:JSON.stringify({vehicle_fixed_profile:2})});assert(response.ok);
 cars=await (await window.fetch('/api/vehicles')).json();assert.equal(cars.fixed_profile,2);assert.equal(cars.recognized_slot,2);assert.equal(cars.profiles[1].soc,null);
 response=await window.fetch('/api/vehicles/estimate',{method:'POST',body:JSON.stringify({slot:2,start_soc:25})});assert(response.ok);
 cars=await response.json();assert.equal(cars.profiles[1].soc,25);assert(cars.profiles[1].soc_estimated);
 response=await window.fetch('/api/config',{method:'POST',body:JSON.stringify({vehicle_fixed_profile:4})});assert.equal(response.status,400);
 const invalid=await response.json();assert.equal(invalid.field,'vehicle_fixed_profile');
 response=await window.fetch('/api/config',{method:'POST',body:JSON.stringify({vehicle_fixed_profile:0})});assert(response.ok);
 cars=await (await window.fetch('/api/vehicles')).json();assert.equal(cars.recognized_slot,1);assert.equal(cars.profiles[1].soc,null,'estimate is discarded on selection-mode changes');
 console.log('PASS: demo showcases PV/battery/car/plan; heat pump stays selectable and defaults off');
})().catch(e=>{console.error(e);process.exitCode=1;});
