const fs=require('node:fs'),vm=require('node:vm'),assert=require('node:assert/strict');
const source=fs.readFileSync(__dirname+'/../integrations/solis/esp-lease-bridge.js','utf8');
const root='0_userdata.0.SolisAkku',req=root+'.entladungSperren',input='mqtt.0.wallbox-ems.solis.hold.request';
let now=Date.now(),tick;const handlers=new Map(),writes=[],published=[];
const data=new Map([[req,false],[root+'.espOwned',false],[root+'.gesperrt',false],[root+'.wirdGeschaltet',false],[root+'.fehler',''],['modbus.0.inputRegisters.33000_TeeNetModel',0x3103]]);
class Clock extends Date{static now(){return now;}}
vm.runInNewContext(source,{Date:Clock,JSON,Number,Promise,Math,log:()=>{},on:(f,cb)=>handlers.set(f.id,cb),onStop:()=>{},setInterval:cb=>tick=cb,getStateAsync:async id=>({val:data.get(id),ack:true,q:0,ts:now}),setStateAsync:async(id,s)=>{data.set(id,s.val);if(id===req){writes.push(s.val);data.set(root+'.gesperrt',s.val);}},sendTo:(_a,_b,m)=>published.push(JSON.parse(m.message))});
const settle=()=>new Promise(r=>setTimeout(r,10));
async function message(hold,seq=1,session='0123456789abcdef',age=0){handlers.get(input)({state:{val:JSON.stringify({session,seq,hold,sent_at:Math.floor(now/1000)-age})}});await settle();}
(async()=>{await settle();assert.equal(writes.length,0,'startup must not change VIS');
 await message(true,1,undefined,20);assert.equal(writes.length,0,'stale activation ignored');
 await message(true);assert.deepEqual(writes,[true]);
 now+=3000;await message(true);assert.deepEqual(writes,[true],'heartbeat must not repeat switching');
 now+=16000;tick();await settle();assert.deepEqual(writes,[true,false],'expired ESP lease must release');
 data.set(req,true);data.set(root+'.gesperrt',true);await message(false,2);assert.equal(data.get(req),true,'unowned release must preserve manual VIS hold');
 await message(true,3);assert.equal(writes.length,2,'manual VIS hold must not be taken over');
 data.set(req,false);data.set(root+'.gesperrt',false);await message(false,4);await message(true,3);assert.equal(writes.length,2,'older command must not replay');
 console.log('PASS: default off, stale activation rejected, no repeated physical commands, expiry releases, VIS ownership preserved, out-of-order command rejected.');
})().catch(e=>{console.error(e);process.exit(1);});
