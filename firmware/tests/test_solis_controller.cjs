const fs=require('node:fs'),vm=require('node:vm'),assert=require('node:assert/strict');
const source=fs.readFileSync(__dirname+'/../integrations/solis/solis-control.js','utf8');
const root='0_userdata.0.SolisAkku',key={mode:'43110_TeeNetMode',reserve:'43024_TeeNetReserveSOC',charge:'43117_TeeNetChargeLimit',power:'43129_TeeNetDischargePower',grid:'43132_TeeNetGridRemote',remote:'43135_TeeNetBatteryRemote'};
for(const k in key)key[k]='modbus.0.holdingRegisters.'+key[k];
async function settle(check){for(let n=0;n<3000;n++){if(check())return;await new Promise(r=>setImmediate(r));}throw Error('Mock did not settle');}
async function make(options={}){
 let now=Date.now(),handler,interval;class Clock extends Date{static now(){return now+=100;}}
 const data=options.data||new Map(Object.entries({[key.mode]:33,[key.reserve]:70,[key.charge]:1000,[key.power]:250,[key.grid]:0,[key.remote]:0,[root+'.entladungSperren']:false,[root+'.gesperrt']:false,[root+'.vorherigeEinstellungen']:'','modbus.0.inputRegisters.33000_TeeNetModel':0x3103,'modbus.0.inputRegisters.35000_TeeNetFamily':0x2030}));
 const writes=[],ctx=vm.createContext({Date:Clock,console,Promise,Set,JSON,Number,Error,Boolean,setInterval:cb=>interval=cb,setTimeout:cb=>setImmediate(cb),log:()=>{},on:(_f,cb)=>handler=cb,
 getStateAsync:async id=>{const remote=data.get(key.remote)===2;let v=data.get(id);
 if(id.endsWith('33149_Batterie_Power'))v=remote?(options.charging?1500:108):4200;
 if(id.endsWith('33135_Batterie_Ladung_01'))v=remote&&options.charging?0:1;
 if(id.endsWith('33057_PV_Leistung'))v=options.charging?5000:3;
 return {val:v,ack:true,q:options.staleModel&&id.endsWith('33000_TeeNetModel')?128:0,ts:Clock.now()};},
 setStateAsync:async(id,s)=>{writes.push([id,s.val,s.ack]);if(!(options.rejectRemote&&id===key.remote&&s.val===2))data.set(id,s.val);}});
 vm.runInContext(source,ctx);await settle(()=>data.get(root+'.wirdGeschaltet')===false&&!!data.get(root+'.status')&&(!data.get(root+'.entladungSperren')||data.get(root+'.gesperrt')));
 return {data,writes,options,toggle:async value=>{data.set(root+'.entladungSperren',value);handler({state:{val:value}});await vm.runInContext('queue',ctx);},tick:async ms=>{now+=ms;interval();await vm.runInContext('queue',ctx);}};
}
(async()=>{
 const a=await make();assert.equal(a.writes.filter(w=>w[2]===false).length,0,'unselected startup must not write Solis');
 await a.toggle(true);assert.equal(a.data.get(key.remote),2);assert.equal(a.data.get(key.power),0);assert.equal(a.data.get(key.charge),1000);assert.equal(a.data.get(key.reserve),70);assert.equal(a.data.get(key.mode),33);assert.equal(a.data.get(root+'.gesperrt'),true);
 const physical=a.writes.filter(w=>w[2]===false);assert.equal(physical[0][0],key.power,'zero target must precede enable');assert.equal(physical[1][0],key.remote);
 await a.tick(61000);assert.equal(a.writes.filter(w=>w[0]===key.remote&&w[1]===2&&w[2]===false).length,2,'refresh must write unchanged enable value');
 await a.toggle(false);assert.equal(a.data.get(key.remote),0);assert.equal(a.data.get(key.power),250);assert.equal(a.data.get(root+'.vorherigeEinstellungen'),'');
 assert.ok(!a.writes.some(w=>w[2]===false&&[key.charge,key.reserve,key.mode].includes(w[0])),'normal operation must preserve charge/reserve/mode');
 const b=await make({rejectRemote:true});await b.toggle(true);assert.equal(b.data.get(key.remote),0);assert.equal(b.data.get(key.power),250);assert.equal(b.data.get(root+'.gesperrt'),false);assert.ok(b.data.get(root+'.fehler'));
 const c=await make({charging:true});await c.toggle(true);assert.equal(c.data.get(root+'.gesperrt'),true,'PV charge must not be mistaken for discharge');await c.toggle(false);
 const legacy=new Map(a.data);legacy.set(key.mode,17);legacy.set(key.charge,10);legacy.set(key.reserve,100);legacy.set(root+'.entladungSperren',true);legacy.set(root+'.vorherigeEinstellungen',JSON.stringify({version:1,mode:33,reserve:70,charge:1000}));
 const d=await make({data:legacy});assert.equal(d.data.get(key.mode),33);assert.equal(d.data.get(key.charge),1000);assert.equal(d.data.get(key.reserve),70);assert.equal(JSON.parse(d.data.get(root+'.vorherigeEinstellungen')).version,2);await d.toggle(false);
 const e=await make();await e.toggle(true);e.options.staleModel=true;await e.tick(10000);assert.equal(e.data.get(key.remote),0,'stale model must release owned remote command');assert.equal(e.data.get(root+'.entladungSperren'),false);
 console.log('PASS: explicit selection only; zero before enable; charge limits preserved; enable heartbeat; release restores target; rejected write rolls back; PV charging accepted; old 1-A profile migrated; stale feedback releases.');
})().catch(e=>{console.error(e);process.exit(1);});
