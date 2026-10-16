const fs=require('node:fs'),vm=require('node:vm'),assert=require('node:assert/strict');
const schema=JSON.parse(fs.readFileSync('preview/expert-schema.json','utf8'));
function demo(store){
 const location={origin:'http://demo.local',search:''};
 const window={location,fetch:async path=>{assert.equal(path,'/expert-schema.json','Unexpected real network');return new Response(JSON.stringify(schema));}};
 const context={window,location,URL,URLSearchParams,Response,Date,Math,Number,String,Map,JSON,document:{addEventListener(){}},localStorage:{getItem:key=>store.get(key)||null,setItem:(key,value)=>store.set(key,value)}};
 for(const file of ['preview/demo-api.js','tools/evcc-demo-api.js','tools/expert-demo-api.js'])vm.runInNewContext(fs.readFileSync(file,'utf8'),context);
 return {read:async path=>(await window.fetch(path)).json(),post:async(path,data)=>window.fetch(path,{method:'POST',body:JSON.stringify(data)})};
}
(async()=>{
 const store=new Map([['teenet-demo-terms','2026-10-09.2']]);let d=demo(store);
 await d.post('/api/control',{enabled:false,mode:'off'});
 const info=await(await d.post('/api/expert',{pin:'4040',action:'read_gpio'})).json();assert.equal(info.roles.length,12);assert.equal(info.pins.length,49);
 assert.equal((await d.post('/api/config',{phase_feedback_pin:15})).status,403);
 assert.equal((await d.post('/api/expert',{pin:'4040',action:'gpio',values:{xemex_tx_pin:17}})).status,400);
 assert.equal((await d.post('/api/expert',{pin:'4040',action:'gpio',values:{relay1_pin:0}})).status,400);
 let r=await d.post('/api/expert',{pin:'4040',action:'gpio',values:{xemex_tx_pin:8,xemex_rx_pin:9,phase_feedback_pin:15}});assert.equal(r.status,200,await r.clone().text());
 assert.equal((await d.read('/api/config')).phase_feedback_pin,15);
 let s=await d.read('/api/status');assert.equal(s.gpio_pins.phase_feedback_pin,13);assert(s.reboot_required&&s.expert_parameters_modified);
 d=demo(store);s=await d.read('/api/status');assert(s.reboot_required&&s.expert_parameters_modified);assert.equal(s.gpio_pins.phase_feedback_pin,13);
 await d.post('/api/reboot',{});s=await d.read('/api/status');assert(!s.reboot_required&&s.expert_parameters_modified);assert.equal(s.gpio_pins.phase_feedback_pin,15);
 d=demo(store);assert.equal((await d.read('/api/config')).xemex_tx_pin,8);assert.equal((await d.read('/api/status')).gpio_pins.xemex_tx_pin,8);
 assert.equal((await d.post('/api/expert',{pin:'wrong',action:'gpio',values:{xemex_tx_pin:10}})).status,403);
 console.log('PASS: GPIO preview uses the protected endpoint, rejects reserved/conflicting pins, persists all roles through reload, keeps old runtime pins until reboot and retains deviation markers');
})().catch(error=>{console.error(error);process.exitCode=1;});
