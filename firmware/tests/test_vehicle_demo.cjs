const assert=require('assert/strict'),fs=require('fs'),vm=require('vm');
let state={terms_accepted:true,car_soc_pct:31,charge_plan:{active:false}},posted;
const config={vehicle_soc_enabled:true,mqtt_enabled:true,vehicle_fixed_profile:0};
const memory=new Map(),context={window:{fetch:async(path,options={})=>{if(path==='/api/config'){if(options.method==='POST')Object.assign(config,JSON.parse(options.body));return new Response(JSON.stringify(config));}if(path==='/api/plan'){posted=JSON.parse(options.body);state.charge_plan=posted.resume?{...state.charge_plan,active:true}:{active:true,target_kwh:posted.kwh,delivered_kwh:0,deadline_epoch:posted.deadline_epoch};}return new Response(JSON.stringify(state),{headers:{'Content-Type':'application/json'}});}},location:{origin:'http://demo.local'},Response,URL,structuredClone,Uint8Array,atob,localStorage:{getItem:k=>memory.get(k)||null,setItem:(k,v)=>memory.set(k,v)}};
vm.runInNewContext(fs.readFileSync('tools/vehicle_demo_api.js','utf8'),context);
async function call(path,body){const r=await context.window.fetch(path,body?{method:'POST',body:JSON.stringify(body)}:{});return [r.status,await r.json()];}
(async()=>{
 const [status,s]=await call('/api/vehicles');assert.equal(status,200);assert.equal(s.enabled,false);assert.equal(s.recognized_slot,0);
 let feature=await call('/api/vehicles',{enabled:true,revision:1});assert.equal(feature[0],200);assert.equal(feature[1].enabled,true);assert.equal(feature[1].recognized_slot,0);
 feature=await call('/api/vehicles',{enabled:false,revision:2});assert.equal(feature[0],200);assert.equal(feature[1].enabled,false);
 const profile={name:'Auto',plate:'AB 1',cards:['DEMO-CARD-1',''],capacity_kwh:60,maximum_kw:11,efficiency:.9,target_soc:80,soc_source:2,picture:3};
 let r=await call('/api/vehicles',{slot:1,enabled:true,revision:3,profile});assert.equal(r[0],200);assert.equal(r[1].recognized_slot,1);assert.equal(r[1].profiles[0].soc,31);assert.equal(r[1].profiles[0].picture,3);
 r=await call('/api/vehicles',{slot:2,enabled:true,revision:4,profile});assert.equal(r[0],400);
 r=await call('/api/vehicles/plan',{slot:2,current_soc:31,target_soc:80,deadline_epoch:Date.now()/1000+14400});assert.equal(r[0],400);
 const deadline=Math.floor(Date.now()/1000)+14400;
 r=await call('/api/vehicles/plan',{slot:1,current_soc:20,target_soc:80,deadline_epoch:deadline});assert.equal(r[0],200);assert(Math.abs(posted.kwh-32.6666666667)<.0001);
 assert.equal(r[1].plan.slot,1);assert.equal(r[1].plan.estimated_soc,31,'measured SOC must not be replaced with an estimate');
 const bad=await call('/api/vehicles/plan',{slot:1,current_soc:31,target_soc:'80',deadline_epoch:deadline});assert.equal(bad[0],400);
 state.car_soc_pct=null;state.charge_plan.delivered_kwh=5;
 r=await call('/api/vehicles/plan',{slot:1,current_soc:40,target_soc:80,deadline_epoch:deadline,resume:true});assert.equal(r[0],200);assert.equal(r[1].plan.estimated_soc,38.5,'resume keeps its original energy baseline');
 r=await call('/api/plan',{active:true,resume:true});assert.equal(r[0],400,'generic resume must not discard the RFID binding');
 r=await call('/api/plan',{active:true,kwh:10,deadline_epoch:deadline});assert.equal(r[0],200);r=await call('/api/vehicles');assert.equal(r[1].plan.active,false,'explicit generic replacement clears the old vehicle plan');
 console.log('PASS: isolated demo profiles persist, recognize assigned card, reject duplicate cards/unmatched plans, use measured car SOC, validate deadline and resume without double-counting');
})().catch(e=>{console.error(e);process.exitCode=1;});
