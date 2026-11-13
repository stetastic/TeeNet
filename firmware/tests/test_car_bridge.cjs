const vm=require('node:vm'),fs=require('node:fs'),assert=require('node:assert/strict');
const source=fs.readFileSync('tools/iobroker-teenet-car.js','utf8').replace(/\nupdateVehicleSoc\(\);\s*$/,'');
let now=1800000000000,state={val:97,ts:now,ack:true,q:0},broker=true,alive=true,connected=true,fail=false,stop;
const sent=[],warnings=[];
const ctx=vm.createContext({Date:class extends Date{static now(){return now;}},Math,Number,Promise,Error,String,Set,
 on(){},setInterval(){return 1;},clearInterval(){},onStop(fn){stop=fn;},log(message){warnings.push(message);},
 sendTo(adapter,method,message){assert.equal(adapter,'mqtt.0');assert.equal(method,'sendMessage2Client');assert.equal(message.retain,false);assert.match(message.topic,/^wallbox-ems\/input\/vehicle_[1-4]_soc_pct$/);sent.push(message);},
 async getStateAsync(id){
  if(id==='system.adapter.mqtt.0.alive')return {val:broker};
  if(id==='system.adapter.mercedesme.0.alive')return {val:alive};
  if(id==='mercedesme.0.info.connection')return {val:connected};
  if(id==='test.second')return {val:43,ts:now,ack:true,q:0};
  if(fail)throw Error('source read failed');
  assert.match(id,/\.soc\.intValue$/);return state;
 }});
vm.runInContext(source,ctx);
async function update(){await vm.runInContext('updateVehicleSoc()',ctx);}
const last=slot=>sent.filter(s=>s.topic.endsWith(`vehicle_${slot}_soc_pct`)).at(-1)?.message;
(async()=>{
 await update();assert.equal(sent.length,4);assert.equal(last(1),'97');for(let i=2;i<=4;i++)assert.equal(last(i),'unavailable');
 await update();assert.equal(sent.length,4);now+=60000;await update();assert.equal(sent.length,8);
 for(const val of [0,100,45.5]){state={val,ts:now,ack:true,q:0};await update();assert.equal(last(1),String(val));}
 for(const invalid of [{val:101},{val:-1},{val:'97'},{val:NaN},{val:Infinity},{ack:false},{q:1},{ts:now-1200001},{ts:now+3000}]){
  state={val:97,ts:now,ack:true,q:0};await update();state={...state,...invalid};await update();assert.equal(last(1),'unavailable');
 }
 state={val:97,ts:now,ack:true,q:0};await update();connected=false;await update();assert.equal(last(1),'unavailable');connected=true;await update();assert.equal(last(1),'97');
 alive=false;await update();assert.equal(last(1),'unavailable');alive=true;await update();
 fail=true;await update();assert.equal(last(1),'unavailable');await update();assert.equal(warnings.length,1);fail=false;await update();assert.equal(last(1),'97');
 broker=false;const before=sent.length;await update();assert.equal(sent.length,before);broker=true;await update();assert.equal(sent.length,before+4);
 vm.runInContext("VEHICLE_SOURCES[1]={id:'test.second',alive:VEHICLE_SOURCES[0].alive,connection:VEHICLE_SOURCES[0].connection}",ctx);await update();assert.equal(last(1),'97');assert.equal(last(2),'43');assert.equal(last(3),'unavailable');
 let done=false;stop(()=>done=true);assert(done);for(let i=1;i<=4;i++)assert.equal(last(i),'unavailable');const stopped=sent.length;await update();assert.equal(sent.length,stopped);
 console.log('PASS: four isolated SOC topics, 0–100%, no retain, stale/invalid/disconnected sources unavailable, heartbeat, broker recovery, stop invalidation and no charging commands');
})().catch(e=>{console.error(e);process.exitCode=1});
