/* Local synthetic preview only. Never forwards expert requests to hardware. */
(() => {
 const previous=window.fetch.bind(window),storage='teenet-demo-expert-v1';
 let saved={},active={},wrongUntil=0,phaseMqttAllowed=false,actuatorScan=null,outputSetup=null,testUntil=0,testGoal=0;
 try{saved=JSON.parse(localStorage.getItem(storage)||'{}');active=JSON.parse(localStorage.getItem(storage+'-active')||'{}');}catch(e){}
 try{outputSetup=JSON.parse(localStorage.getItem(storage+'-phase-mqtt-output')||'null');phaseMqttAllowed=outputSetup?.enabled===true;}catch(e){}
 const json=(v,status=200)=>new Response(JSON.stringify(v),{status,headers:{'Content-Type':'application/json'}});
 const plain=(v,status)=>new Response(v,{status});
 const schemaPromise=previous('/expert-schema.json').then(r=>r.json());
 const outputBoot=(async()=>{if(outputSetup?.enabled){await previous('/api/control',{method:'POST',body:JSON.stringify({enabled:false,mode:'off'})});await previous('/api/config',{method:'POST',body:JSON.stringify(outputSetup.config)});await previous('/api/reboot',{method:'POST',body:'{}'});}})();
let gpioValues={},gpioActive={};
try{gpioValues=JSON.parse(localStorage.getItem(storage+'-gpio')||'{}');gpioActive=JSON.parse(localStorage.getItem(storage+'-gpio-active')||'{}');}catch(e){}
const gpioConfig=(schema,cfg)=>({...schema.defaults,...cfg,...gpioValues});
function gpioRoleUsed(id,c){
 if(id.startsWith('wallbox_'))return Number(c.shell_rs485_interface||0)===0;
 if(id.startsWith('xemex_'))return Number(c.meter_rs485_interface||0)===0&&!['shelly_gen2','shelly_em1','em24_tcp'].includes(c.wallbox_meter_type);
 if(id.startsWith('house_'))return Number(c.house_rs485_interface||0)===0&&['xemex','sdm630','sdm630mct'].includes(c.house_meter_type);
 if(id==='external_mode_input_pin')return !!c.external_mode_input_enabled;
 if(id==='evu_input_pin')return !!c.evu_input_enabled;
 if(id==='relay1_pin')return !!c.relay_board_enabled&&Number(c.phase_actuator_type||0)===0;
 if(id==='phase_feedback_pin'||id==='phase_feedback_second_pin')return !!c.phase_switch_enabled&&Number(c.phase_actuator_type||0)===0&&(id==='phase_feedback_pin'||Number(c.phase_feedback_count)===2);
 return Number(c.status_led_pin)!==-1;
}
function gpioSnapshot(schema,cfg){return {...schema.gpio,roles:schema.gpio.roles.map(r=>({...r,value:cfg[r.id]??r.default,active:gpioActive[r.id]??cfg[r.id]??r.default,used:gpioRoleUsed(r.id,cfg)}))};}
function gpioValid(schema,cfg){
 const pins=new Set();for(const r of schema.gpio.roles){const pin=cfg[r.id]??r.default,entry=schema.gpio.pins.find(p=>p.pin===pin);
  if(!Number.isInteger(pin)||(r.led&&pin===-1?false:!entry||(r.led?!entry.led_allowed:!!entry.reason)))return false;
  if(gpioRoleUsed(r.id,cfg)&&pin!==-1){if(pins.has(pin))return false;pins.add(pin);}
 }return true;
}
function gpioPending(schema,cfg){return schema.gpio.roles.some(r=>(cfg[r.id]??r.default)!==(gpioActive[r.id]??cfg[r.id]??r.default));}
function gpioRemember(schema,cfg,next){
 if(!Object.keys(gpioActive).length)gpioActive=Object.fromEntries(schema.gpio.roles.map(r=>[r.id,cfg[r.id]??r.default]));
 const values=Object.fromEntries(schema.gpio.roles.map(r=>[r.id,next[r.id]??r.default]));
 localStorage.setItem(storage+'-gpio',JSON.stringify(values));localStorage.setItem(storage+'-gpio-active',JSON.stringify(gpioActive));gpioValues=values;
}

 function phaseSupported(cfg,method=outputSetup?.transport||0){return !!((method>0||cfg.mqtt_enabled)&&(['shelly_gen2','em24_tcp','sdm630','sdm630mct'].includes(cfg.wallbox_meter_type)||(cfg.wallbox_meter_type==='xemex'&&[2,3].includes(Number(cfg.xemex_coils)))));}
 function outputTopic(cfg){return outputSetup?.topic||((cfg.mqtt_prefix||'wallbox-ems')+'/output/single_phase');}
 function trueThree(){return outputSetup?.true_three_phase===true;}
 function outputExtra(){return {phase_output_transport:outputSetup?.transport||0,phase_output_host:outputSetup?.host||'',phase_output_switch_id:outputSetup?.switch_id||0,phase_output_password_set:!!outputSetup?.password};}
 function hostValid(host){return typeof host==='string'&&/^[a-zA-Z0-9][a-zA-Z0-9.-]{0,62}$/.test(host);}
 function topicValid(topic,cfg){return typeof topic==='string'&&topic.length>0&&topic.length<128&&!/[\x00-\x1f\x7f#+]/.test(topic)&&topic[0]!=='$'&&!(topic.startsWith(cfg.mqtt_prefix+'/')&&/^(command\/|input\/|sensor\/|state$|availability$)/.test(topic.slice(cfg.mqtt_prefix.length+1)));}
 function valid(schema,values){
  for(const p of schema.parameters){const value=values[p.id]??p.default;if(!Number.isFinite(value)||value<p.min-1e-5||value>p.max+1e-5||Math.abs((value-p.min)/p.step-Math.round((value-p.min)/p.step))>.002)return false;}
  const get=id=>values[id]??schema.parameters.find(p=>p.id===id).default;
  return get('PHASE_STOP_MAX')>get('PHASE_ZERO')&&get('PHASE_DATA_MAX')>get('PHASE_SETTLE')&&get('EVCC_LEASE')>get('EVCC_STATUS_TTL');
 }
 window.fetch=async(input,options={})=>{
  await outputBoot;
  const url=new URL(typeof input==='string'?input:input.url,location.origin);
  if(url.origin===location.origin&&url.pathname==='/api/phase-test'){
   if(options.method!=='POST')return plain('POST erforderlich.',405);
   let body;try{body=JSON.parse(options.body||'{}');}catch(e){return plain('JSON ungueltig.',400);}
   if(body.stop===true){testUntil=testGoal=0;return json({ok:true});}
   if(body.pin!=='4040'||body.confirm_risk!==true||![1,3].includes(body.phases))return plain('Warnung bestaetigen und Passwort 4040 eingeben.',400);
   const status=await (await window.fetch('/api/status')).json();
   if(!status.phase_test_ready)return plain(status.phase_test_block||'Schuetztest derzeit nicht bereit.',409);
   testGoal=body.phases;testUntil=Date.now()+90000;return json({ok:true});
  }
  if(testUntil>Date.now()&&options.method==='POST'&&['/api/config','/api/expert'].includes(url.pathname)){
   const body=JSON.parse(options.body||'{}');if(url.pathname==='/api/config'||body.action!=='read')return plain('Schuetztest zuerst beenden.',409);
  }
  if(testUntil>Date.now()&&url.pathname==='/api/control'&&options.method==='POST'){
   const body=JSON.parse(options.body||'{}');if(body.enabled===true)return plain('Schuetztest laeuft. Ladung gesperrt.',409);if(body.enabled===false)testUntil=testGoal=0;
  }
  if(url.origin===location.origin&&url.pathname==='/api/theme'){
   if(options.method==='POST'){
    const body=JSON.parse(options.body||'{}');
    if(['light','dark'].includes(body.theme))localStorage.setItem('teennet-theme',body.theme);
    if(['standard','graphic'].includes(body.view))localStorage.setItem('teennet-view',body.view);
    if(['standard','compact'].includes(body.density))localStorage.setItem('teennet-density',body.density);
    if(body.graphic_notice_ack===true)localStorage.setItem('teennet-graphic-notice','1');
   }
   return json({theme:localStorage.getItem('teennet-theme')||'unset',view:localStorage.getItem('teennet-view')||'standard',density:localStorage.getItem('teennet-density')||'standard',graphic_notice_pending:localStorage.getItem('teennet-graphic-notice')!=='1'});
  }
  if(url.origin===location.origin&&url.pathname==='/api/meters/scan'){
   if(options.method==='POST'){
    let request;try{request=JSON.parse(options.body||'{}');}catch(e){return plain('JSON ungueltig.',400);}
    if(request.family==='shelly_switch'){
     actuatorScan={family:'shelly_switch',running:false,partial:false,progress:100,devices:[
      {host:'192.168.1.88',id:'shellypro1-demo',name:'Shelly Pro 1 · Demo',model:'SPSW-001XE16EU',protected:true,compatible:true,profile:0,inputs:2,modbus:false,contacts_checked:false},
      {host:'192.168.1.89',id:'shelly1g4-demo',name:'Shelly 1 Gen4 · Demo',model:'S4SW',protected:false,compatible:true,profile:4,inputs:1,modbus:true,contacts_checked:false}
     ]};return json(actuatorScan);
    }
    actuatorScan=null;
   }else if(actuatorScan)return json(actuatorScan);
  }
  if(url.origin===location.origin&&url.pathname==='/api/config'&&options.method==='POST'){
   let request;try{request=JSON.parse(options.body||'{}');}catch(e){return plain('JSON ungueltig.',400);}
   const cfg=gpioConfig(await schemaPromise,await (await previous('/api/config')).json()),next={...cfg,...request};
   if('phase_mqtt_output_enabled' in request||'phase_mqtt_topic' in request||'phase_mqtt_true_three_phase' in request||['phase_output_transport','phase_output_host','phase_output_switch_id','phase_output_password'].some(k=>k in request)||Number(next.phase_actuator_type)===2&&Number(cfg.phase_actuator_type)!==2)return plain('Ohne Rueckmeldung im Expertenmodus gesondert freigeben.',403);
   const fieldTypes=(await schemaPromise).config_fields;
   for(const [key,value] of Object.entries(request)){
    const kind=fieldTypes[key],valid=kind==='b'?typeof value==='boolean':kind==='s'||kind==='p'?typeof value==='string':
     ['f','i','u'].includes(kind)&&typeof value==='number'&&Number.isFinite(value)&&value>=-5000&&value<=38400&&(kind==='f'||Number.isInteger(value))&&(kind!=='u'||value>=0&&value<=255);
    if(!valid)return plain(`Fehlerhafte Einstellungsdaten (${key}). Seite neu laden und erneut speichern.`,400);
   }
   const gpioSchema=await schemaPromise;for(const r of gpioSchema.gpio.roles.slice(2))if(r.id in request&&request[r.id]!==cfg[r.id])return plain('GPIO-Belegung im Expertenmodus aendern.',403);
   if(!gpioValid(gpioSchema,next))return plain('GPIO-Belegung ungueltig.',400);
   request.relay_active_low=false;if(Number(next.phase_feedback_count)===2)request.phase_feedback_closed_is_single=false;request.phase_feedback_enabled=Number(next.phase_actuator_type)!==2;request.relay1_mode=next.phase_switch_enabled?3:0;
   if(![0,1,2,3].includes(Number(next.phase_actuator_type||0)))return plain('Schuetzprofil ungueltig.',400);
   if(next.phase_shelly_host&&!/^[a-zA-Z0-9][a-zA-Z0-9.-]{0,62}$/.test(next.phase_shelly_host))return plain('Shelly-Adresse pruefen.',400);
   if([1,3].includes(Number(next.phase_actuator_type))&&next.phase_switch_enabled){
    if(!next.phase_shelly_host)return plain('Shelly-Adresse fehlt. Im Bereich Schuetzsteuerung eintragen.',400);
    if(!(['shelly_gen2','em24_tcp','sdm630','sdm630mct'].includes(next.wallbox_meter_type)||(next.wallbox_meter_type==='xemex'&&[2,3].includes(Number(next.xemex_coils)))))return plain(next.wallbox_meter_type==='xemex'?`Wallbox-Zaehler: Xemex mit ${next.xemex_coils} Messspule(n) eingestellt. Fuer die Shelly-Phasenumschaltung mindestens L1 und L2 messen.`:`Wallbox-Zaehlerprofil '${next.wallbox_meter_type}' erfasst nicht mindestens L1 und L2. Fuer die Shelly-Phasenumschaltung ein passendes Profil waehlen.`,400);
    request.phase_feedback_enabled=true;
    const count=Number(next.phase_feedback_count||1),model=Number(next.phase_shelly_model||0),inputs=[2,2,1,1,1,2,2,2,3,4,1,1,1][model]||0;
    if(!inputs||![1,2].includes(count)||count>inputs||(next.phase_shelly_pulse&&(count!==2||Number(next.phase_actuator_type)===3))||(Number(next.phase_actuator_type)===3&&model!==4))return plain('Shelly-Modell, Kontakte und Schuetzansteuerung passen nicht zusammen.',400);
   }
   const status=await (await previous('/api/status')).json();
   if(status.enabled&&['phase_actuator_type','phase_shelly_host','phase_shelly_password','phase_feedback_count','phase_feedback_closed_is_single','phase_shelly_model','phase_shelly_pulse','phase_shelly_true_three_phase'].some(key=>key in request&&request[key]!==cfg[key]))return plain('Schuetzanbindung nur bei gestoppter Ladung aendern.',409);
   const response=await previous(input,{...options,body:JSON.stringify(request)});
   if(response.ok&&['wallbox_tx_pin','wallbox_rx_pin'].some(k=>k in request))gpioRemember(gpioSchema,cfg,{...cfg,...request});
   if(response.ok&&outputSetup){
    Object.assign(outputSetup.config,request);
    if(Number(next.phase_actuator_type)!==2||next.phase_switch_enabled===false||next.relay_board_enabled===false){phaseMqttAllowed=outputSetup.enabled=false;await previous('/api/control',{method:'POST',body:JSON.stringify({enabled:false,mode:'off'})});}
    localStorage.setItem(storage+'-phase-mqtt-output',JSON.stringify(outputSetup));
   }
   return response;
  }
  if(url.origin===location.origin && url.pathname==='/api/expert'){
   if(options.method!=='POST')return plain('POST erforderlich.',405);
   let body;try{body=JSON.parse(options.body||'{}');}catch(e){return plain('JSON ungueltig.',400);}
   const revoke=body.action==='mqtt_phase_output'&&body.enabled===false;
   if(!revoke&&Date.now()<wrongUntil)return plain('Kurz warten und erneut versuchen.',429);
   if(!revoke&&body.pin!=='4040'){wrongUntil=Date.now()+2000;return plain('Experten-Passwort falsch.',403);}
   if(body.action==='authorize')return json({ok:true});
   const schema=await schemaPromise;
   if(!valid(schema,saved)){saved={};active={};}
   const cfg=gpioConfig(await schemaPromise,await (await previous('/api/config')).json());
   if(body.action==='read')return json({values:schema.parameters.map(p=>saved[p.id]??p.default),active:schema.parameters.map(p=>active[p.id]??p.default),pending:schema.parameters.some(p=>(saved[p.id]??p.default)!==(active[p.id]??p.default))});
   if(body.action==='read_gpio')return json(gpioSnapshot(schema,cfg));
   const status=await (await previous('/api/status')).json();
   if(body.action==='gpio'){
    if(status.enabled||status.evcc_enabled||status.phase_switching||!status.meter_ok||(status.actual_a||[]).length!==3||(status.actual_a||[]).some(a=>!Number.isFinite(a)||a<0||a>=1))return plain('Ladung zuerst stoppen.',409);
    if(!body.values||!Object.keys(body.values).length||Object.keys(body.values).some(k=>!schema.gpio.roles.some(r=>r.id===k)))return plain('GPIO-Belegung ungueltig.',400);
    const next={...cfg,...body.values};if(!gpioValid(schema,next))return plain('Reservierte oder doppelt belegte GPIOs.',400);
    try{gpioRemember(schema,cfg,next);}catch(e){return plain('Speichern fehlgeschlagen.',500);}
    await previous('/api/control',{method:'POST',body:JSON.stringify({enabled:false,mode:'off'})});
    return json({ok:true,restart_needed:gpioPending(schema,next),reboot_required:gpioPending(schema,next)||!!status.reboot_required});
   }
   if(body.action==='mqtt_phase_output'){
    const method=body.transport===undefined?0:body.transport,id=body.switch_id===undefined?0:body.switch_id;
    if(body.enabled&&body.shelly_model!==undefined&&(!Number.isInteger(body.shelly_model)||body.shelly_model<0||body.shelly_model>12||method===0||id!==0||method===1&&![4,12].includes(body.shelly_model)))return plain('Shelly-Modell und Verbindung prüfen.',400);
    const suppliedPassword=typeof body.password==='string'?body.password:'';const storedPassword=outputSetup?.host===body.host?outputSetup?.password||'':'';
    const endpoint=Number.isInteger(method)&&method>=0&&method<=3&&Number.isInteger(id)&&[0,100].includes(id)&&(method===0?topicValid(body.topic,cfg):hostValid(body.host))&&(body.password===undefined||typeof body.password==='string'&&body.password.length<=64)&&(method!==3||!!(suppliedPassword||storedPassword));
    if(typeof body.enabled!=='boolean'||(body.enabled&&(body.confirm_risk!==true||body.confirm_no_feedback!==true||body.repeat_pin!=='4040'||!endpoint||typeof body.true_three_phase!=='boolean')))return plain('Warnungen bestaetigen und Experten-Passwort erneut eingeben. Schalttopic und Schuetztyp pruefen.',400);
    if(body.enabled&&phaseMqttAllowed&&(body.true_three_phase!==trueThree()||method!==(outputSetup?.transport||0)||id!==(outputSetup?.switch_id||0)||(method>0&&body.host!==outputSetup?.host)||(method===3&&suppliedPassword&&suppliedPassword!==outputSetup?.password)))return plain('Vor Aenderung des Schuetztyps die MQTT-Freigabe ausschalten.',409);
    if(body.enabled&&(status.enabled||status.evcc_enabled||status.phase_switching||!status.meter_ok||(status.actual_a||[]).length!==3||(status.actual_a||[]).some(a=>typeof a!=='number'||!Number.isFinite(a)||a<0||a>=1)||!phaseSupported(cfg,method)))return plain('Ladung stoppen sowie MQTT-Konfiguration und mindestens L1/L2 prüfen.',409);
    let activationRestartNeeded=false;
    if(!body.enabled){phaseMqttAllowed=false;await previous('/api/control',{method:'POST',body:JSON.stringify({enabled:false,mode:'off'})});
     const response=await previous('/api/config',{method:'POST',body:JSON.stringify({phase_actuator_type:0,phase_feedback_enabled:true})});
     if(response.ok){const result=await response.clone().json();activationRestartNeeded=!!result.restart_needed;}if(outputSetup)outputSetup.enabled=false;}
    else{
     const pendingBefore=!!status.reboot_required;
     const config={basic_mode:false,phase_actuator_type:2,relay_board_enabled:true,phase_switch_enabled:true,relay1_mode:3,phase_feedback_enabled:false,mqtt_enabled:cfg.mqtt_enabled,wallbox_meter_type:cfg.wallbox_meter_type,xemex_coils:cfg.xemex_coils};
     if(body.shelly_model!==undefined)config.phase_shelly_model=body.shelly_model;
     const response=await previous('/api/config',{method:'POST',body:JSON.stringify(config)});if(!response.ok)return response;
     const result=await response.clone().json();activationRestartNeeded=!!result.restart_needed;
     outputSetup={enabled:true,topic:body.topic||outputTopic(cfg),true_three_phase:body.true_three_phase,transport:method,host:body.host||'',switch_id:id,password:method===3?(suppliedPassword||storedPassword):'',config};phaseMqttAllowed=!pendingBefore&&!activationRestartNeeded;
    }
    try{localStorage.setItem(storage+'-phase-mqtt-output',JSON.stringify(outputSetup));}catch(e){phaseMqttAllowed=false;return plain('Freigabe nicht gespeichert.',500);}
    const current=await (await previous('/api/status')).json();return json({ok:true,reboot_required:!!current.reboot_required,restart_needed:activationRestartNeeded});
   }
   if(status.enabled||status.evcc_enabled||status.phase_switching||!status.meter_ok||(status.actual_a||[0,0,0]).some(a=>a>=1))return plain('Ladung zuerst stoppen; frische Messwerte unter 1 A erforderlich.',409);
   if(body.action==='config')return previous('/api/config',{...options,body:JSON.stringify(body.changes)});
   if(body.action!=='save'&&body.action!=='reset')return plain('Aktion ungueltig.',400);
   const next=body.action==='reset'?{}:{...saved,...body.values};
   if(Object.keys(next).some(key=>!schema.parameters.some(p=>p.id===key))||!valid(schema,next)||(next.MIN_CURRENT??8.7)>cfg.max_charge_a)return plain('Regelparameter und Grenzen pruefen.',400);
   const changed=schema.parameters.some(p=>(next[p.id]??p.default)!==(saved[p.id]??p.default));saved=next;
   try{localStorage.setItem(storage,JSON.stringify(saved));}catch(e){return plain('Speichern fehlgeschlagen.',500);}
   const pending=schema.parameters.some(p=>(saved[p.id]??p.default)!==(active[p.id]??p.default));
   return json({ok:true,reboot_required:!!status.reboot_required||pending,restart_needed:changed&&pending});
  }
  if(url.origin===location.origin && url.pathname==='/api/reboot'&&options.method==='POST'){
   testUntil=testGoal=0;
   const response=await previous(input,options);if(response.ok){active={...saved};gpioActive={...gpioValues};phaseMqttAllowed=outputSetup?.enabled===true;try{localStorage.setItem(storage+'-gpio-active',JSON.stringify(gpioActive));localStorage.setItem(storage+'-active',JSON.stringify(active));}catch(e){}}return response;
  }
  const response=await previous(input,options);
  if(url.origin===location.origin&&url.pathname==='/api/config'&&response.ok){
   const cfg=gpioConfig(await schemaPromise,await response.clone().json());cfg.phase_shelly_password_set=!!cfg.phase_shelly_password;delete cfg.phase_shelly_password;
   Object.assign(cfg,{phase_mqtt_topic:outputTopic(cfg),phase_mqtt_output_enabled:phaseMqttAllowed,phase_mqtt_true_three_phase:trueThree(),...outputExtra()});return json(cfg);
  }
  if(url.origin===location.origin && url.pathname==='/api/status'&&response.ok){
   const state=await response.clone().json(),schema=await schemaPromise;
   state.expert_parameters_modified=schema.parameters.some(p=>Math.abs((saved[p.id]??p.default)-p.default)>1e-4);
   const cfg=gpioConfig(await schemaPromise,await (await previous('/api/config')).json());
   const gp=gpioSnapshot(schema,cfg);state.gpio_pins=Object.fromEntries(gp.roles.map(r=>[r.id,r.active]));
   state.hardware='ESP32-S3 N16R8';state.hardware_id='esp32s3-n16r8';
   state.gpio_setup_allowed=!state.enabled&&!state.evcc_enabled&&!state.phase_switching&&state.meter_ok&&(state.actual_a||[]).length===3&&state.actual_a.every(a=>Number.isFinite(a)&&a>=0&&a<1);
   state.expert_parameters_modified||=gp.roles.some(r=>r.value!==r.default);state.reboot_required||=gpioPending(schema,cfg);
   Object.assign(state,outputExtra());state.phase_output_base_ready=phaseSupported(cfg,1);state.phase_output_meter_ready=phaseSupported(cfg,1);state.phase_output_measured_phases=cfg.wallbox_meter_type==='xemex'?Number(cfg.xemex_coils):3;state.phase_feedback_count=Number(cfg.phase_feedback_count||1);state.phase_output_mqtt_ready=!!cfg.mqtt_enabled;state.phase_output_connected=phaseMqttAllowed;
   state.phase_mqtt_allowed=phaseMqttAllowed;state.phase_mqtt_ready=phaseSupported(cfg);state.phase_mqtt_output_topic=outputTopic(cfg);state.phase_mqtt_true_three_phase=trueThree();state.phase_mqtt_output_single=Number(cfg.phase_actuator_type)===2&&phaseMqttAllowed?state.charge_phases===1:null;
   state.phase_mqtt_output_on=state.phase_mqtt_output_single===null?null:(trueThree()?!state.phase_mqtt_output_single:state.phase_mqtt_output_single);
   state.phase_actuator_type=Number(cfg.phase_actuator_type||0);
   if(state.phase_actuator_type===2){state.phase_feedback_enabled=false;if(!phaseMqttAllowed){state.enabled=false;state.mode='off';state.block_reason='phase_switch';state.phase_state='fehler';}}
   state.phase_test_active=testUntil>Date.now();state.phase_test_goal=state.phase_test_active?testGoal:0;
   if(state.phase_test_active){state.enabled=false;state.evcc_enabled=false;state.charge_phases=testGoal;state.phase_feedback=testGoal;state.phase_state='bereit';state.block_reason='phase_test';}
   if([1,3].includes(state.phase_actuator_type)){
    state.phase_shelly_connected=state.phase_shelly_config_ok=!!cfg.phase_shelly_host;
    state.phase_shelly_no_closed=cfg.phase_feedback_closed_is_single?state.charge_phases===1:state.charge_phases===3;state.phase_shelly_nc_closed=!state.phase_shelly_no_closed;
    state.phase_shelly_output=cfg.phase_shelly_pulse?false:(cfg.phase_shelly_true_three_phase?state.charge_phases===3:state.charge_phases===1);state.phase_feedback_enabled=true;
   }
   const contactsReady=state.phase_switch_enabled&&state.phase_feedback_enabled&&state.phase_actuator_type!==2&&(![1,3].includes(state.phase_actuator_type)||state.phase_shelly_connected);
   state.phase_contact1_closed=contactsReady?(cfg.phase_feedback_closed_is_single?state.charge_phases===1:state.charge_phases===3):null;
   state.phase_contact2_closed=contactsReady&&state.phase_feedback_count===2?!state.phase_contact1_closed:null;
   if(!contactsReady)state.phase_feedback=0;
   if(schema.parameters.some(p=>(saved[p.id]??p.default)!==(active[p.id]??p.default)))state.reboot_required=true;
   const stopped=!state.enabled&&!state.evcc_enabled&&!state.phase_switching&&state.meter_ok&&(state.actual_a||[]).length===3&&state.actual_a.every(a=>Number.isFinite(a)&&a>=0&&a<1);
   state.phase_output_setup_allowed=stopped;
   state.phase_test_ready=stopped&&!state.reboot_required&&!state.phase_test_active&&!!cfg.phase_switch_enabled&&(state.phase_actuator_type===0||state.phase_actuator_type===2&&phaseMqttAllowed||[1,3].includes(state.phase_actuator_type)&&!!cfg.phase_shelly_host);
   state.phase_test_block=state.phase_test_ready?'':!cfg.phase_switch_enabled?'Phasenumschaltung zuerst einrichten und speichern.':state.reboot_required?'Einstellungen gespeichert. Zuerst neu starten.':!stopped?'Ladung stoppen und aktuelle Strommessung prüfen.':'Shelly-Verbindung oder externe Freigabe fehlt.';
   if(active.MIN_CURRENT)state.min_charge_a=active.MIN_CURRENT;
   return json(state);
  }
  return response;
 };
})();
