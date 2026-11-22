const vm=require('vm'),fs=require('fs'),assert=require('assert');
const source=fs.readFileSync('tools/iobroker-teenet-battery.js','utf8').replace('updateBattery();setInterval(updateBattery,5000);','');
async function check(soc=21,socAge=0){
 const sent=[],reads=[];
 const ctx=vm.createContext({Date,Math,Number,Promise,Error,String,log(){},sendTo(b,c,m){assert.equal(m.retain,false);sent.push(m)},async getStateAsync(id){reads.push(id);assert.equal(id,'javascript.0.SOC Korrigiert');return {val:soc,ts:Date.now()-socAge,q:0,ack:true};}});
 vm.runInContext(source,ctx);await vm.runInContext('updateBattery()',ctx);
 assert.deepEqual(reads,['javascript.0.SOC Korrigiert']);
 return Object.fromEntries(sent.map(m=>[m.topic.split('/').pop(),m.message]));
}
(async()=>{
 assert.deepEqual(await check(),{battery_soc_pct:'21'});
 assert.deepEqual(await check(38,180000),{battery_soc_pct:'38'});
 assert.deepEqual(await check(38,250000),{battery_soc_valid:'false'});
 assert.deepEqual(await check(-2),{battery_soc_pct:'0'});
 assert.deepEqual(await check(101),{battery_soc_valid:'false'});
 console.log('PASS: only corrected SOC read; negative clamped; stale and high values rejected');
})().catch(e=>{console.error(e);process.exit(1)});
