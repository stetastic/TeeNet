const fs=require('node:fs'),vm=require('node:vm'),assert=require('node:assert/strict');
const schema=JSON.parse(fs.readFileSync('preview/expert-schema.json','utf8'));
assert.equal(fs.readFileSync('preview/expert-demo-api.js','utf8'),fs.readFileSync('tools/expert-demo-api.js','utf8'),'preview must use new local API, not old published shim');
function demo(store){
 const location={origin:'http://demo.local',search:''},window={location,fetch:async path=>{
  assert.equal(path,'/expert-schema.json','No request to hardware allowed');return new Response(JSON.stringify(schema));}};
 const context={window,location,URL,URLSearchParams,Response,Date,Math,Number,String,Map,JSON,document:{addEventListener(){}},localStorage:{getItem:key=>store.get(key)||null,setItem:(key,value)=>store.set(key,value)}};
 for(const file of ['preview/demo-api.js','preview/evcc-demo-api.js','preview/expert-demo-api.js'])vm.runInNewContext(fs.readFileSync(file,'utf8'),context);
 return {read:async path=>(await window.fetch(path)).json(),post:async(path,data)=>window.fetch(path,{method:'POST',body:JSON.stringify(data)})};
}
(async()=>{
 const store=new Map([['teenet-demo-terms','2026-10-09.2'],['teenet-demo-expert-phase-mqtt','true']]);let d=demo(store);
 assert.equal((await d.read('/api/status')).phase_mqtt_allowed,false,'Old incoming permission never activates new outgoing control');
 const expert=data=>d.post('/api/expert',{pin:'4040',action:'mqtt_phase_output',...data});
 assert.equal((await expert({enabled:true})).status,400);
 await d.post('/api/control',{enabled:false,mode:'off'});
 let response=await d.post('/api/config',{basic_mode:false,mqtt_enabled:true,xemex_coils:2});assert.equal(response.status,200);
 const yes={enabled:true,topic:'wallbox-ems/output/single_phase',repeat_pin:'4040',confirm_risk:true,confirm_no_feedback:true,true_three_phase:false};
 assert.equal((await expert({...yes,repeat_pin:'1111'})).status,400);
 assert.equal((await expert({...yes,true_three_phase:'true'})).status,400);
 assert.equal((await expert({...yes,topic:'wallbox-ems/command/phases'})).status,400,'Own inbound topics cannot become a switch output');
 assert.equal((await d.post('/api/config',{phase_mqtt_output_enabled:true})).status,403,'Ordinary config cannot bypass warnings/password');
 assert.equal((await d.post('/api/config',{phase_mqtt_true_three_phase:true})).status,403);
 response=await expert(yes);assert.equal(response.status,200,await response.clone().text());
 let activation=await response.json();assert(activation.reboot_required&&activation.restart_needed,'Setup can be stored while another restart is pending');
 assert.equal((await d.read('/api/config')).phase_actuator_type,2);assert.equal((await d.read('/api/status')).phase_mqtt_allowed,false,'Saved setup activates only after reboot');await d.post('/api/reboot',{});
 assert((await d.read('/api/status')).phase_mqtt_allowed);assert.equal((await d.read('/api/status')).phase_mqtt_requested,undefined);
 assert.equal((await d.read('/api/status')).phase_mqtt_output_topic,yes.topic);
 let status=await d.read('/api/status');assert.equal(status.phase_mqtt_true_three_phase,false);assert.equal(status.phase_mqtt_output_on,status.charge_phases===1);
 assert.equal((await expert({...yes,true_three_phase:true})).status,409,'Polarity cannot change while allowed');
 const reopened=demo(store);assert((await reopened.read('/api/status')).phase_mqtt_allowed,'New permission and actuator profile persist together');
 assert.equal((await reopened.read('/api/config')).phase_actuator_type,2);
 assert.equal((await d.post('/api/expert',{pin:'4040',action:'mqtt_phases',enabled:true,confirm_risk:true,confirm_no_feedback:true})).status,400,'Retired function cannot be called');
 await d.post('/api/control',{enabled:true,mode:'manual'});assert.equal((await expert(yes)).status,409);
 assert.equal((await expert({enabled:false})).status,200,'Disable is possible while charging');
 assert.equal((await d.read('/api/status')).enabled,false);assert.equal((await d.read('/api/status')).phase_mqtt_output_single,null);
 assert.equal((await d.read('/api/config')).phase_actuator_type,0,'Disabling returns to a normal feedback profile');await d.post('/api/reboot',{});
 await d.post('/api/control',{enabled:false,mode:'off'});
 response=await expert({...yes,true_three_phase:true});assert.equal(response.status,200,await response.clone().text());
 await d.post('/api/reboot',{});
 status=await d.read('/api/status');assert.equal(status.phase_mqtt_true_three_phase,true);assert.equal(status.phase_mqtt_output_on,status.charge_phases===3);
 d=demo(store);assert.equal((await d.read('/api/status')).phase_mqtt_true_three_phase,true,'NO polarity survives reload');
 assert.equal((await expert({enabled:false})).status,200);
 d=demo(store);assert.equal((await d.read('/api/status')).phase_mqtt_allowed,false,'Revocation persists on reload');
 assert.equal((await d.read('/api/status')).phase_mqtt_true_three_phase,true,'Revocation does not erase stored polarity');
 await d.post('/api/config',{basic_mode:false,mqtt_enabled:false,xemex_coils:2});await d.post('/api/reboot',{});
 for(const method of [1,2,3]){
  response=await expert({...yes,transport:method,host:'192.168.1.88',switch_id:0,shelly_model:method===1?4:0,...(method===3?{password:'synthetic-secret'}:{})});assert.equal(response.status,200,await response.clone().text());
  await d.post('/api/reboot',{});
  status=await d.read('/api/status');assert.equal(status.phase_output_transport,method);assert.equal(status.phase_output_mqtt_ready,false);
  const privateState=await d.read('/api/config');assert.equal(privateState.phase_output_host,'192.168.1.88');assert.equal(privateState.phase_shelly_model,method===1?4:0,'Selected model persists with the protected setup');if(method===3){assert(privateState.phase_output_password_set);assert.equal(privateState.phase_output_password,undefined);}
  assert.equal((await d.post('/api/config',{phase_output_host:'192.168.1.99'})).status,403);
  assert.equal((await expert({...yes,transport:method,host:'192.168.1.99',...(method===3?{password:'synthetic-secret'}:{})})).status,409);
  assert.equal((await expert({enabled:false})).status,200);
  await d.post('/api/reboot',{});
 }
 console.log('PASS: private demo MQTT/Modbus/RPC, no Shelly password or hardware request, transport/host persistence and protection, isolated phase inputs, stopped gates and immediate revocation');
})().catch(error=>{console.error(error);process.exitCode=1;});

