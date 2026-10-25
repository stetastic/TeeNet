/* Demo only: synthetic heat-pump meter and MQTT values. No device or broker is contacted. */
(() => {
  'use strict';
  const previous=window.fetch.bind(window),storage='teenet-demo-heatpump-v1';
  const defaults={heatpump_enabled:false,heatpump_source:'shelly_gen2',heatpump_host:'heatpump.demo.local',heatpump_input_topic:'wallbox-ems/input/heatpump_power_w',heatpump_publish_enabled:true,heatpump_output_topic:'wallbox-ems/sensor/heatpump_power_w'};
  const fields=new Set(Object.keys(defaults)),sources=new Set(['shelly_gen2','shelly_em1','mqtt']);
  let cfg={...defaults},sample=null,physicalOverride=null,baseFlags={basic_mode:true,mqtt_enabled:false};
  const started=Date.now();
  const json=(data,status=200)=>new Response(JSON.stringify(data),{status,headers:{'Content-Type':'application/json'}});
  const topicValid=value=>typeof value==='string'&&value.trim()===value&&value.length>0&&value.length<128&&!/[\x00-\x1f\x7f#+]/.test(value)&&value[0]!=='$';
  const hostValid=value=>typeof value==='string'&&/^[a-zA-Z0-9][a-zA-Z0-9.-]{0,62}$/.test(value);
  function valid(value){
    if(typeof value.heatpump_enabled!=='boolean'||typeof value.heatpump_publish_enabled!=='boolean'||!sources.has(value.heatpump_source)||typeof value.heatpump_host!=='string'||typeof value.heatpump_input_topic!=='string'||typeof value.heatpump_output_topic!=='string')return 'Wärmepumpen-Einstellungen prüfen.';
    if(!value.heatpump_enabled)return '';
    if(value.heatpump_source!=='mqtt'&&!hostValid(value.heatpump_host))return 'Adresse des Wärmepumpen-Zählers eintragen.';
    if(value.heatpump_source==='mqtt'&&!topicValid(value.heatpump_input_topic))return 'MQTT-Eingangstopic der Wärmepumpe prüfen.';
    if(value.heatpump_publish_enabled&&!topicValid(value.heatpump_output_topic))return 'MQTT-Ausgangstopic der Wärmepumpe prüfen.';
    if(value.heatpump_publish_enabled&&value.heatpump_input_topic===value.heatpump_output_topic)return 'Für MQTT-Eingang und -Ausgang unterschiedliche Topics verwenden.';
    return '';
  }
  try{
    const stored=JSON.parse(localStorage.getItem(storage)||'null');
    if(stored&&typeof stored==='object'&&!Array.isArray(stored)){
      const restored={...defaults,...Object.fromEntries(Object.entries(stored).filter(([key])=>fields.has(key)))};
      if(!valid(restored))cfg=restored;
    }
  }catch{}
  function effective(base){
    for(const key of ['basic_mode','mqtt_enabled'])if(typeof base[key]==='boolean')baseFlags[key]=base[key];
    // Selecting basic operation must not reactivate an old optional meter later.
    if(baseFlags.basic_mode)cfg.heatpump_enabled=false;
    return {...cfg,heatpump_enabled:!baseFlags.basic_mode&&cfg.heatpump_enabled};
  }
  function emit(name,detail){try{window.dispatchEvent(new CustomEvent(name,{detail}));}catch{}}
  async function requestBody(input,options){
    if(options.body!==undefined)return JSON.parse(options.body||'{}');
    if(typeof input!=='string'&&typeof input.clone==='function')return JSON.parse(await input.clone().text()||'{}');
    return {};
  }
  function physicalWatts(){
    const seconds=(Date.now()-started)/1000;
    return physicalOverride??Math.round(1800+320*Math.sin(seconds/21)+100*Math.sin(seconds/7));
  }
  function measurement(base,physical){
    const enabled=effective(base).heatpump_enabled;
    const age=sample?Math.max(0,sample.age_s):0;
    const mqttReady=baseFlags.mqtt_enabled;
    const validSample=!sample||sample.valid&&age<=30;
    const ok=enabled&&validSample&&(cfg.heatpump_source!=='mqtt'||mqttReady);
    const watts=ok?physical:null;
    const publishing=ok&&cfg.heatpump_publish_enabled&&mqttReady;
    return {heatpump_enabled:enabled,heatpump_ok:ok,heatpump_w:watts,heatpump_source:cfg.heatpump_source,heatpump_age_s:ok?age:null,heatpump_publish_enabled:cfg.heatpump_publish_enabled,heatpump_publish_ok:publishing,heatpump_mqtt_topic:cfg.heatpump_output_topic,heatpump_input_topic:cfg.heatpump_input_topic,heatpump_published_w:publishing?watts:null,heatpump_demo:true};
  }
  window.TeeNetHeatpumpDemo={simulation:true};
  window.fetch=async(input,options={})=>{
    const url=new URL(typeof input==='string'?input:input.url,location.origin);
    if(url.origin!==location.origin)return previous(input,options);
    const method=String(options.method||input?.method||'GET').toUpperCase();
    if(url.pathname==='/api/demo/heatpump'){
      if(method==='GET')return json({simulation:true,sample:sample?{...sample}:null});
      if(method!=='POST')return json({error:'POST erforderlich.'},405);
      let body;try{body=await requestBody(input,options);}catch{return json({error:'JSON ungültig.'},400);}
      if(!body||typeof body!=='object'||Array.isArray(body)||typeof body.valid!=='boolean'||(body.power_w!==undefined&&(!Number.isFinite(body.power_w)||body.power_w<0||body.power_w>50000))||(body.valid&&body.power_w===undefined)||(body.age_s!==undefined&&(!Number.isFinite(body.age_s)||body.age_s<0)))return json({error:'Nichtnegative Leistung in Watt und Messwertstatus angeben.'},400);
      if(body.power_w!==undefined)physicalOverride=body.power_w;
      sample={power_w:physicalWatts(),valid:body.valid,age_s:body.age_s??0};
      emit('teenet:heatpump-sample',{...sample});
      return json({ok:true,simulation:true,...sample});
    }
    if(url.pathname==='/api/config'&&method==='POST'){
      let body;try{body=await requestBody(input,options);}catch{return json({error:'JSON ungültig.'},400);}
      if(!body||typeof body!=='object'||Array.isArray(body))return json({error:'Einstellungsdaten prüfen.'},400);
      if(Object.keys(body).some(key=>key.startsWith('heatpump_')&&!fields.has(key)))return json({error:'Unbekannte Wärmepumpen-Einstellung.'},400);
      const baseResponse=await previous('/api/config');
      if(!baseResponse.ok)return baseResponse;
      const base=await baseResponse.json(),old=effective(base),next={...old,...Object.fromEntries(Object.entries(body).filter(([key])=>fields.has(key)))};
      if((body.basic_mode??base.basic_mode)===true)next.heatpump_enabled=false;
      const error=valid(next);if(error)return json({error},400);
      const core=Object.fromEntries(Object.entries(body).filter(([key])=>!fields.has(key)));
      const response=await previous(input,{...options,method:'POST',body:JSON.stringify(core)});
      if(response.ok){
        let result;try{result=await response.clone().json();}catch{return response;}
        if(result?.ok===false||result?.error)return json({error:result.error||'Einstellungen wurden nicht gespeichert.'},409);
        if(next.heatpump_source!==cfg.heatpump_source)sample=null;
        cfg=next;
        for(const key of ['basic_mode','mqtt_enabled'])if(typeof core[key]==='boolean')baseFlags[key]=core[key];
        try{localStorage.setItem(storage,JSON.stringify(cfg));}catch{}
        emit('teenet:heatpump-config',{...cfg});
      }
      return response;
    }
    const response=await previous(input,options);
    if(!response.ok)return response;
    if(url.pathname==='/api/config'&&method==='GET'){
      const base=await response.json();return json({...base,...effective(base)});
    }
    if(url.pathname==='/api/status'&&method==='GET'){
      const base=await response.json(),physical=physicalWatts(),meter=measurement(base,physical);
      // The original mock house_power_w is signed grid exchange, not house use.
      // Its real load remains even if its optional display or measurement is unavailable.
      // The graphic separates it from residual house use only with a valid measurement.
      if(base.house_meter_ok===true&&Number.isFinite(base.house_power_w))base.house_power_w+=physical;
      return json({...base,...meter});
    }
    return response;
  };
})();
