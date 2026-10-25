const fs=require('node:fs'),vm=require('node:vm'),assert=require('node:assert/strict');
const source=fs.readFileSync('tools/heatpump-demo-api.js','utf8');

function fixture({storageError=false,stored=null}={}){
  const config={basic_mode:true,mqtt_enabled:true,mqtt_prefix:'wallbox-ems'},calls=[],events=[],storage=new Map();
  if(stored)storage.set('teenet-demo-heatpump-v1',JSON.stringify(stored));
  let rejectSave=false;
  const previous=async(input,options={})=>{
    const path=String(typeof input==='string'?input:input.url),method=options.method||'GET';
    calls.push({path,method,body:options.body});
    if(path==='/api/config'){
      if(method==='POST'){
        if(rejectSave)return rejectSave==='semantic'?Response.json({ok:false,error:'Save rejected'}):new Response('Save rejected',{status:409});
        const body=JSON.parse(options.body);assert(!Object.keys(body).some(key=>key.startsWith('heatpump_')),'demo fields never reach the firmware API');Object.assign(config,body);
        return Response.json({ok:true,restart_needed:false});
      }
      return Response.json({...config});
    }
    if(path.startsWith('/api/status'))return Response.json({basic_mode:config.basic_mode,house_meter_ok:true,house_power_w:-4200,pv_generation_w:7000,battery_charge_w:0,battery_discharge_w:0,wallbox_w:1500});
    return new Response('Not found',{status:404});
  };
  const window={fetch:previous,dispatchEvent:event=>events.push(event)};
  const context=vm.createContext({window,Response,URL,Number,Set,JSON,Object,Math,Date:class extends Date{static now(){return 1000000;}},location:{origin:'http://demo.test'},CustomEvent:class{constructor(type,init){this.type=type;this.detail=init.detail;}},localStorage:{getItem(key){if(storageError)throw Error('blocked');return storage.get(key)||null;},setItem(key,value){if(storageError)throw Error('blocked');storage.set(key,value);}}});
  vm.runInContext(source,context);
  return {config,calls,events,storage,fail(value){rejectSave=value;},get:path=>window.fetch(path).then(r=>r.json()),post:(path,body)=>window.fetch(path,{method:'POST',body:JSON.stringify(body)})};
}
const configure=(h,extra={})=>h.post('/api/config',{basic_mode:false,heatpump_enabled:true,...extra});

(async()=>{
  const h=fixture();
  assert.equal((await h.get('/api/config')).heatpump_enabled,false,'fresh installations have no optional meter');
  let status=await h.get('/api/status');assert.equal(status.house_power_w,-2400,'the physical heat pump exists before its optional display is enabled');assert.equal(status.heatpump_w,null);assert.equal(status.heatpump_enabled,false);
  assert((await configure(h)).ok);status=await h.get('/api/status?profile=dashboard');
  assert.equal(status.heatpump_ok,true);assert(status.heatpump_w>1300&&status.heatpump_w<2300);
  assert.equal(status.house_power_w,-4200+status.heatpump_w,'the extra consumer appears exactly once in grid exchange');
  const residual=status.pv_generation_w+status.house_power_w-status.wallbox_w-status.heatpump_w;
  assert.equal(residual,1300,'residual house use excludes both wallbox and heat pump');
  assert.equal(status.heatpump_published_w,status.heatpump_w);
  assert.equal(status.heatpump_mqtt_topic,'wallbox-ems/sensor/heatpump_power_w');
  assert.equal(JSON.parse(h.storage.get('teenet-demo-heatpump-v1')).heatpump_enabled,true);
  assert.equal(h.events.at(-1).type,'teenet:heatpump-config');

  assert((await h.post('/api/demo/heatpump',{power_w:2400,valid:true})).ok);
  status=await h.get('/api/status');assert.equal(status.heatpump_w,2400);assert.equal(status.house_power_w,-1800);
  assert((await h.post('/api/demo/heatpump',{power_w:0,valid:true})).ok);
  status=await h.get('/api/status');assert.equal(status.heatpump_w,0);assert.equal(status.heatpump_ok,true);assert.equal(status.heatpump_published_w,0);assert.equal(status.house_power_w,-4200,'a measured zero remains valid');
  assert((await h.post('/api/demo/heatpump',{power_w:2400,valid:false})).ok);
  status=await h.get('/api/status');assert.equal(status.heatpump_ok,false);assert.equal(status.heatpump_w,null);assert.equal(status.heatpump_published_w,null);assert.equal(status.house_power_w,-1800,'unknown meter data does not remove the physical load');
  assert((await h.post('/api/demo/heatpump',{valid:false})).ok);
  assert.equal((await h.get('/api/status')).house_power_w,-1800,'a missing value retains the prior physical 2400 W load');
  assert((await h.post('/api/demo/heatpump',{power_w:2400,valid:true,age_s:31})).ok);
  status=await h.get('/api/status');assert.equal(status.heatpump_ok,false,'stale samples remain unknown');assert.equal(status.house_power_w,-1800,'stale meter data does not erase physical consumption');
  for(const body of [{power_w:-1,valid:true},{power_w:'20',valid:true},{valid:true},{power_w:Infinity,valid:true},{power_w:100,valid:'yes'}])assert.equal((await h.post('/api/demo/heatpump',body)).status,400);

  assert((await h.post('/api/config',{mqtt_enabled:false})).ok);
  await h.post('/api/demo/heatpump',{power_w:1000,valid:true});
  status=await h.get('/api/status');assert.equal(status.heatpump_ok,true,'Shelly measurement does not require MQTT');assert.equal(status.heatpump_published_w,null,'disabled MQTT never claims a published value');
  assert((await h.post('/api/config',{heatpump_source:'mqtt'})).ok);
  status=await h.get('/api/status');assert.equal(status.heatpump_ok,false,'MQTT input cannot be ready while MQTT is disabled');assert.equal(status.house_power_w,-3200,'changing the source or losing MQTT does not change physical consumption');
  assert((await h.post('/api/config',{mqtt_enabled:true})).ok);
  await h.post('/api/demo/heatpump',{power_w:3200,valid:true});
  status=await h.get('/api/status');assert.equal(status.heatpump_w,3200);assert.equal(status.heatpump_published_w,3200);
  assert((await h.post('/api/config',{heatpump_publish_enabled:false})).ok);
  assert.equal((await h.get('/api/status')).heatpump_published_w,null);

  const before=h.storage.get('teenet-demo-heatpump-v1');h.fail(true);
  assert.equal((await h.post('/api/config',{heatpump_enabled:false})).status,409);
  assert.equal(h.storage.get('teenet-demo-heatpump-v1'),before,'failed base saves do not persist the overlay');
  assert.equal((await h.get('/api/config')).heatpump_enabled,true);
  h.fail('semantic');assert.equal((await h.post('/api/config',{heatpump_enabled:false})).status,409);assert.equal(h.storage.get('teenet-demo-heatpump-v1'),before,'HTTP 200 with ok=false is not a successful save');
  h.fail(false);
  for(const body of [{heatpump_source:'unsupported'},{heatpump_source:'shelly_em1',heatpump_host:'http://192.168.1.20/path'},{heatpump_source:'mqtt',heatpump_input_topic:'bad/+'},{heatpump_publish_enabled:true,heatpump_input_topic:'same',heatpump_output_topic:'same'},{heatpump_unknown:true}])assert.equal((await h.post('/api/config',body)).status,400);
  assert.equal(h.storage.get('teenet-demo-heatpump-v1'),before,'invalid saves do not change persisted configuration');

  assert((await h.post('/api/config',{heatpump_enabled:false})).ok);
  status=await h.get('/api/status');assert.equal(status.heatpump_enabled,false);assert.equal(status.house_power_w,-1000,'hiding the meter leaves the physical 3200 W load in the house total');
  assert.equal(status.pv_generation_w+status.house_power_w-status.wallbox_w,4500,'hidden heat-pump consumption returns to the unsplit house total');
  assert((await h.post('/api/config',{heatpump_enabled:true})).ok);
  status=await h.get('/api/status');assert.equal(status.house_power_w,-1000);assert.equal(status.heatpump_w,3200);assert.equal(status.pv_generation_w+status.house_power_w-status.wallbox_w-status.heatpump_w,1300,'showing it again only splits the same house total');

  assert((await h.post('/api/config',{basic_mode:true})).ok);
  status=await h.get('/api/status');assert.equal(status.heatpump_enabled,false);assert.equal(status.house_power_w,-1000,'basic-mode selection does not switch a physical appliance off');
  assert((await h.post('/api/config',{basic_mode:false})).ok);
  assert.equal((await h.get('/api/config')).heatpump_enabled,false,'reopening extra functions does not reactivate the heat pump');
  assert(h.calls.every(call=>call.path.startsWith('/api/config')||call.path.startsWith('/api/status')),'Shelly hosts and MQTT brokers are never contacted');
  assert.equal((await h.post('https://elsewhere.test/api/demo/heatpump',{power_w:100,valid:true})).status,404,'another origin never receives the local simulation API');
  assert.equal((await h.post('/api/demo/heatpump-extra',{power_w:100,valid:true})).status,404,'only the exact simulation endpoint is intercepted');
  const blocked=fixture({storageError:true});assert((await configure(blocked)).ok);assert.equal((await blocked.get('/api/status')).heatpump_enabled,true,'blocked localStorage does not break the demo');
  const stale=fixture({stored:{heatpump_enabled:true}});assert.equal((await stale.get('/api/config')).heatpump_enabled,false);assert((await stale.post('/api/config',{basic_mode:false})).ok);assert.equal((await stale.get('/api/config')).heatpump_enabled,false,'basic defaults discard a stale saved activation');
  console.log('heatpump demo API: configuration, MQTT simulation, balance, invalid/stale input, failed saves and basic-mode defaults passed');
})().catch(error=>{console.error(error);process.exitCode=1;});
