const fs=require('node:fs'),vm=require('node:vm'),assert=require('node:assert/strict');
const schema=JSON.parse(fs.readFileSync('preview/expert-schema.json','utf8'));
const storage='teenet-demo-expert-v1';
function demo(store){
 const location={origin:'http://demo.local',search:''};
 const window={location,fetch:async path=>{
  assert.equal(path,'/expert-schema.json','Unexpected real network');
  return new Response(JSON.stringify(schema));
 }};
 const context={window,location,URL,URLSearchParams,Response,Date,Math,Number,String,Map,JSON,
  document:{addEventListener(){}},localStorage:{getItem:key=>store.get(key)||null,setItem:(key,value)=>store.set(key,value)}};
 for(const file of ['preview/demo-api.js','tools/evcc-demo-api.js','tools/expert-demo-api.js'])vm.runInNewContext(fs.readFileSync(file,'utf8'),context);
 return {read:async path=>(await window.fetch(path)).json(),post:async(path,data)=>{
  const response=await window.fetch(path,{method:'POST',body:JSON.stringify(data)});
  assert.equal(response.status,200,await response.clone().text());return response.json();
 }};
}
(async()=>{
 const store=new Map([['teenet-demo-terms','2026-10-09.2'],[storage,JSON.stringify({PV_START:20})]]);
 let d=demo(store);
 assert.equal((await d.read('/api/status')).reboot_required,true,'saved expert change is pending');
 await d.post('/api/reboot',{});
 assert.equal((await d.read('/api/status')).reboot_required,false,'real UI reboot clears expert restart');
 d=demo(store);
 assert.equal((await d.read('/api/status')).reboot_required,false,'active expert values survive page reload');
 const info=await d.post('/api/expert',{pin:'4040',action:'read'});
 assert.equal(info.active[schema.parameters.findIndex(p=>p.id==='PV_START')],20);
 await d.post('/api/control',{enabled:false});
 assert.equal((await d.post('/api/expert',{pin:'4040',action:'save',values:{PV_START:25}})).reboot_required,true);
 assert.equal((await d.post('/api/expert',{pin:'4040',action:'save',values:{PV_START:20}})).reboot_required,false,'reverting to active removes pending restart');
 assert.equal((await d.read('/api/status')).reboot_required,false);
 await d.post('/api/config',{wifi_ssid:'Changed local demo network'});
 const result=await d.post('/api/expert',{pin:'4040',action:'save',values:{PV_START:20}});
 assert.equal(result.restart_needed,false);
 assert.equal(result.reboot_required,true,'expert save preserves pending network restart');
 await d.post('/api/reboot',{});
 assert.equal((await d.read('/api/status')).reboot_required,false);
 console.log('PASS: expert restart, reload, reverting changes and independent network restart');
})().catch(error=>{console.error(error);process.exit(1);});
