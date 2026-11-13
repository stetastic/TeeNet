const fs=require('node:fs'),vm=require('node:vm'),assert=require('node:assert/strict');
const schema=JSON.parse(fs.readFileSync('preview/expert-schema.json','utf8'));
function demo(store){
 const location={origin:'http://demo.local',search:''},window={location,fetch:async path=>{
  assert.equal(path,'/expert-schema.json','No request to hardware allowed');return new Response(JSON.stringify(schema));}};
 const context={window,location,URL,URLSearchParams,Response,Date,Math,Number,String,Map,JSON,document:{addEventListener(){}},localStorage:{getItem:key=>store.get(key)||null,setItem:(key,value)=>store.set(key,value)}};
 for(const file of ['preview/demo-api.js','preview/evcc-demo-api.js','preview/expert-demo-api.js'])vm.runInNewContext(fs.readFileSync(file,'utf8'),context);
 return {read:async path=>(await window.fetch(path)).json(),post:async(path,data)=>window.fetch(path,{method:'POST',body:JSON.stringify(data)})};
}
(async()=>{
 const store=new Map([['teenet-demo-terms','2026-10-09.2']]);let d=demo(store);
 await d.post('/api/control',{enabled:false,mode:'off'});
 let response=await d.post('/api/meters/scan',{family:'shelly_switch'}),scan=await response.json();
 assert.equal(scan.family,'shelly_switch');assert.equal(scan.devices.length,2);
 assert(scan.devices[0].protected&&scan.devices[0].compatible,'Protected Pro 1 remains discoverable');
 assert(scan.devices[1].compatible&&scan.devices[1].inputs===1&&scan.devices[1].modbus,'Gen4 has an RPC/Modbus profile with one input');
 assert.equal((await d.read('/api/meters/scan')).family,'shelly_switch');
 await d.post('/api/meters/scan',{family:'shelly'});
 assert.notEqual((await d.read('/api/meters/scan')).family,'shelly_switch','Meter scan is independent');
 const malformed={basic_mode:false,relay_board_enabled:true,phase_switch_enabled:true,phase_actuator_type:1,mqtt_enabled:'true',phase_shelly_host:'192.168.1.88',xemex_coils:2};
 response=await d.post('/api/config',malformed);assert.equal(response.status,400);assert((await response.text()).includes('mqtt_enabled'));
 let unchanged=await d.read('/api/config');assert.equal(unchanged.phase_actuator_type,0);assert.equal(unchanged.xemex_coils,1,'Malformed input must not partially apply');
 const setup={basic_mode:false,relay_board_enabled:true,phase_switch_enabled:true,relay1_mode:3,phase_actuator_type:1,phase_feedback_source:2,phase_shelly_model:0,phase_shelly_host:'192.168.1.88',wallbox_meter_type:'xemex',xemex_coils:1};
 assert.equal((await d.post('/api/config',setup)).status,400,'At least two measured phase currents are needed for network switching');
 setup.xemex_coils=2;setup.phase_shelly_password='synthetic-test-password';
 response=await d.post('/api/config',setup);assert.equal(response.status,200,await response.clone().text());
 let cfg=await d.read('/api/config');assert(!('phase_feedback_count' in cfg));assert.equal(cfg.phase_shelly_password,undefined);assert(cfg.phase_shelly_password_set);
 await d.post('/api/reboot',{});
 let status=await d.read('/api/status');assert.equal(status.phase_actuator_type,1);assert(!status.phase_feedback_enabled);assert(!('phase_contact1_closed' in status));
 assert(status.phase_test_ready);
 assert.equal((await d.post('/api/phase-test',{phases:1,pin:'4040',confirm_risk:true})).status,200);
 status=await d.read('/api/status');assert.equal(status.charge_phases,1);assert.equal(status.phase_feedback,0);assert(!status.enabled);
 assert.equal((await d.post('/api/phase-test',{stop:true})).status,200);
 assert.equal((await d.post('/api/config',{phase_shelly_pulse:true})).status,400,'Pulse mode stays unavailable');
 for(let model=0;model<13;model++){
  response=await d.post('/api/config',{phase_actuator_type:1,phase_shelly_model:model});assert.equal(response.status,200,await response.clone().text());
  response=await d.post('/api/config',{phase_actuator_type:3,phase_shelly_model:model});assert.equal(response.status,[4,12].includes(model)?200:400);
 }
 await d.post('/api/config',{phase_actuator_type:1});await d.post('/api/reboot',{});
 assert.equal((await d.post('/api/config',{phase_shelly_host:'host/rpc/Switch.Set'})).status,400);
 await d.post('/api/control',{enabled:true,mode:'manual'});
 for(const change of [{phase_actuator_type:0},{phase_shelly_host:'192.168.1.89'},{phase_shelly_password:'different-test-password'}])assert.equal((await d.post('/api/config',change)).status,409,'Active charging locks actuator configuration');
 d=demo(store);cfg=await d.read('/api/config');assert.equal(cfg.phase_actuator_type,1,'Saved phase choices survive page reload');assert.equal((await d.read('/api/status')).enabled,false,'Reload does not resume charging');
 console.log('PASS: private Shelly demo: protected discovery, independent meter search, actual three-phase requirement, no auxiliary contacts and no pulses, password masking and active-charge configuration lock; no hardware calls');
})().catch(error=>{console.error(error);process.exitCode=1;});
