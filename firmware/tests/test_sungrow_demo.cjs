const fs=require('node:fs'),vm=require('node:vm'),assert=require('node:assert/strict');
let cfg={basic_mode:false,sungrow_enabled:false,sungrow_profile:0,sungrow_model:'SH10RT',sungrow_port:502,sungrow_unit_id:1,sungrow_host:'',sungrow_pv:false,sungrow_battery:false,house_meter_type:'tasmota'},outside=0;
const ctx={location:{origin:'http://demo.test'},URL,Response,Number,JSON,window:{fetch:async(input,options={})=>{
 if(String(input).startsWith('https:')){outside++;return new Response('remote');}
 if(String(input)==='/api/config'){if(options.method==='POST')Object.assign(cfg,JSON.parse(options.body));return Response.json(options.method==='POST'?{ok:true}:cfg);}
 if(String(input)==='/api/status')return Response.json({pv_generation_w:6000,battery_soc_pct:73,house_power_w:-2000});
 throw Error('Unexpected request '+input);
}}};
vm.runInNewContext(fs.readFileSync('tools/sungrow-demo-api.js','utf8'),ctx);
const post=async body=>ctx.window.fetch('/api/config',{method:'POST',body:JSON.stringify(body)});
(async()=>{
 let r=await post({sungrow_enabled:true});assert.equal(r.status,400);assert.equal((await r.json()).field,'sungrow_host');assert(!cfg.sungrow_enabled);
 r=await post({sungrow_enabled:true,sungrow_host:'sungrow.demo.local',sungrow_pv:true,sungrow_battery:true});assert.equal(r.status,200);
 let s=await (await ctx.window.fetch('/api/status')).json();assert(s.sungrow_ok&&s.sungrow_model==='SH10RT');assert.equal(s.pv_source,'Sungrow');assert.equal(s.battery_source,'Sungrow');assert.equal(s.pv_generation_w,6000);
 r=await post({sungrow_model:'SG10RT',sungrow_profile:1});assert.equal(r.status,400);assert.equal((await r.json()).field,'sungrow_model');
 r=await post({sungrow_model:'SG10RT',sungrow_profile:1,sungrow_battery:false});assert.equal(r.status,200);
 r=await post({house_meter_type:'sungrow'});assert.equal(r.status,400);assert.equal((await r.json()).field,'house_meter_type');
 for(const body of [{sungrow_port:0},{sungrow_unit_id:248},{sungrow_host:'http://host/path'},{huawei_enabled:true,huawei_pv:true}])assert.equal((await post(body)).status,400);
 await ctx.window.fetch('https://outside.test/api/status');assert.equal(outside,1);
 await post({basic_mode:true});s=await (await ctx.window.fetch('/api/status')).json();assert(!s.sungrow_enabled);
 console.log('PASS: Sungrow demo selects capabilities, rejects invalid saves with field names, preserves balance and never contacts an inverter');
})().catch(e=>{console.error(e);process.exitCode=1;});
