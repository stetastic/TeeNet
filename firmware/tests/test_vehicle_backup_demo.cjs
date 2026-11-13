const fs=require('node:fs'),vm=require('node:vm'),assert=require('node:assert/strict');
const memory=new Map();let quota=false,restoreCalls=0,config={basic_mode:false,price_kwh:.25},state={terms_accepted:true,car_soc_pct:64,charge_plan:{active:false}};
const context={window:{fetch:async(path,options={})=>{
 if(path.startsWith('/api/backup'))return new Response(JSON.stringify({format:'TeeNet-settings',schema:1,settings:config}));
 if(path==='/api/restore'){const b=JSON.parse(options.body);assert.equal(b.schema,1);restoreCalls++;config=b.settings;return new Response('{"ok":true}');}
 return new Response(JSON.stringify(state));
}},Response,URL,structuredClone,Uint8Array,atob,location:{origin:'http://demo.local'},localStorage:{getItem:k=>memory.get(k)||null,setItem:(k,v)=>{if(quota)throw Error('quota');memory.set(k,v);}}};
vm.runInNewContext(fs.readFileSync('tools/vehicle_demo_api.js','utf8'),context);
async function call(path,body){const r=await context.window.fetch(path,body?{method:'POST',body:JSON.stringify(body)}:{});return [r.status,await r.json()];}
(async()=>{
 const p={name:'My car',plate:'AB 123',cards:['DEMO-CARD-1',''],capacity_kwh:60,maximum_kw:11,efficiency:.9,target_soc:80,soc_source:2,picture:3};
 let r=await call('/api/vehicles',{slot:1,enabled:true,revision:1,profile:p});assert.equal(r[0],200);
 r=await call('/api/vehicles/photo?slot=1',{jpeg_base64:'/9j/2Q=='});assert.equal(r[0],200);
 const backup=(await call('/api/backup'))[1];assert.equal(backup.schema,2);assert.equal(backup.vehicles.profiles[0].picture,3);assert.equal(backup.vehicles.photos[0],'/9j/2Q==');
 await call('/api/vehicles',{enabled:false,revision:2});await call('/api/vehicles/photo?slot=1',{jpeg_base64:''});
 r=await call('/api/restore',backup);assert.equal(r[0],200);r=await call('/api/vehicles');assert.equal(r[1].enabled,true);assert.equal(r[1].profiles[0].plate,p.plate);
 const photo=await context.window.fetch('/api/vehicles/photo?slot=1');assert.equal(Buffer.from(await photo.arrayBuffer()).toString('base64'),'/9j/2Q==');
 const invalid=structuredClone(backup);invalid.vehicles.photos[0]='not jpeg';const before=restoreCalls;r=await call('/api/restore',invalid);assert.equal(r[0],400);assert.equal(restoreCalls,before);
 r=await call('/api/restore',{format:'TeeNet-settings',schema:1,settings:{price_kwh:.4}});assert.equal(r[0],200);assert.equal(config.price_kwh,.4);assert.equal((await call('/api/vehicles'))[1].profiles[0].picture,3);
 const current=(await call('/api/vehicles'))[1];quota=true;r=await call('/api/vehicles',{enabled:false,revision:current.revision});assert.equal(r[0],500);quota=false;assert.equal((await call('/api/vehicles'))[1].enabled,true);
 console.log('PASS: schema 2 profile/photo roundtrip, invalid restore has no changes, schema 1 compatibility and quota failure preserves old profiles');
})().catch(e=>{console.error(e);process.exitCode=1;});
