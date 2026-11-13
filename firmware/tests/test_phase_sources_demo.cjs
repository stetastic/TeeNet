const fs=require('node:fs'),vm=require('node:vm'),assert=require('node:assert/strict');
const schema=JSON.parse(fs.readFileSync('preview/expert-schema.json','utf8'));
const store=new Map([['teenet-demo-terms','2026-10-09.2']]);
const location={origin:'http://demo.local',search:''};
function open(){
 const window={location,fetch:async path=>{assert.equal(path,'/expert-schema.json','No hardware requests');return new Response(JSON.stringify(schema));}};
 const context={window,location,URL,URLSearchParams,Response,Date,Math,Number,String,Map,JSON,document:{addEventListener(){}},localStorage:{getItem:k=>store.get(k)||null,setItem:(k,v)=>store.set(k,v)}};
 for(const name of ['demo-api','evcc-demo-api','expert-demo-api'])vm.runInNewContext(fs.readFileSync('preview/'+name+'.js','utf8'),context);
 return window;
}
let window=open();
const read=async path=>(await window.fetch(path)).json();
const post=(path,body)=>window.fetch(path,{method:'POST',body:JSON.stringify(body)});
(async()=>{
 await post('/api/control',{enabled:false,mode:'off'});
 const config={basic_mode:false,relay_board_enabled:true,phase_switch_enabled:true,phase_actuator_type:0,phase_feedback_source:2,xemex_coils:2};
 let response=await post('/api/config',config);assert.equal(response.status,200,await response.clone().text());
 assert((await response.json()).restart_needed);await post('/api/reboot',{});
 let state=await read('/api/status');assert.equal(state.phase_feedback_source,2);assert.equal(state.phase_contact1_closed,undefined);assert(!state.phase_feedback_enabled);
 window=open();state=await read('/api/status');assert.equal(state.phase_feedback_source,2);assert(!('phase_no_feedback_acknowledged' in (await read('/api/config'))));assert(!state.enabled);
 response=await post('/api/config',{phase_actuator_type:2,mqtt_enabled:true,phase_mqtt_topic:'custom/phase/set',phase_mqtt_true_three_phase:true});
 assert.equal(response.status,200,await response.clone().text());assert.equal((await read('/api/status')).phase_mqtt_allowed,false,'New MQTT profile waits for restart');
 await post('/api/reboot',{});state=await read('/api/status');assert(state.phase_mqtt_allowed);assert(state.phase_mqtt_true_three_phase);assert.equal(state.phase_feedback_source,2);
 window=open();state=await read('/api/status');assert(state.phase_mqtt_allowed);assert.equal(state.phase_feedback_source,2);assert(state.phase_mqtt_true_three_phase);
 assert.equal((await post('/api/config',{phase_feedback_source:1,phase_voltage_host:''})).status,400);
 response=await post('/api/config',{phase_feedback_source:1,phase_voltage_host:'192.168.1.89',phase_voltage_protocol:1});assert.equal(response.status,200,await response.clone().text());
 await post('/api/reboot',{});state=await read('/api/status');assert(state.phase_voltage_ok);assert.equal(state.phase_feedback_source,1);assert.equal(state.phase_voltage_v.length,3);assert.equal(state.phase_contact1_closed,undefined);
 await post('/api/control',{enabled:true,mode:'manual'});
 assert.equal((await post('/api/config',{phase_feedback_source:2})).status,409,'Hardware changes remain locked while charging');
 console.log('PASS: normal demo no-feedback and MQTT save/reboot without any expert call or password, no extra warning, voltage proof and active-charge lock');
})().catch(e=>{console.error(e);process.exitCode=1;});
