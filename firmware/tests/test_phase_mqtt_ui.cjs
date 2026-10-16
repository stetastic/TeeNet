const fs=require('node:fs'),vm=require('node:vm'),assert=require('node:assert/strict');
const src=fs.readFileSync('tools/expert_ui.js','utf8'),nodes={};
const el=id=>nodes[id]??={children:[],replaceChildren(){this.children=[];},append(child){this.children.push(child);},checked:false,disabled:false,textContent:'',value:'',open:false,closest(){return this.label??={hidden:false};},showModal(){this.open=true;},close(){this.open=false;},classList:{toggle(){}}};
let prompts=[],requests=[],allowed=false,trueThree=false,savedMethod=0,savedHost='',savedChannel=0,savedPassword='';
const state={enabled:false,reboot_required:false,meter_ok:true,actual_a:[0,0,0],phase_mqtt_allowed:false};
const c={document:{createElement(){return {value:'',textContent:'',disabled:false};}},clearTimeout(){},setTimeout(){throw Error('Unexpected scan polling');},el,state,lastMarkerState:state,configBaseline:{mqtt_enabled:true},window:{teenetShellyPhase:{update(){}}},Event:function(){},chargingActive:()=>state.enabled||!!state.actual_a?.some(a=>a>=1),snapshot:{phase_mqtt_allowed:false,phase_mqtt_ready:true,settings:{mqtt_enabled:true,mqtt_prefix:'my-teenet'}},pin:'4040',working:false,online:true,initialized:true,busy:false,uploading:false,configSaving:false,savedModified:false,
 confirm:()=>prompts.shift(),same:(a,b)=>a===b,renew(){},message(){},updateSaveStatus(){},updateUnsavedFunctions(){},loadConfig:async()=>{},pollStatus:async()=>{},
 api:async(path,body)=>{if(path==='/api/meters/scan')return {family:'shelly_switch',running:false,devices:[{host:'192.168.1.88',name:'Protected Pro 1',profile:0,protected:true,compatible:true}]};requests.push(body);assert.equal(path,'/api/expert');if(body.action==='mqtt_phase_output'){allowed=body.enabled;if(body.enabled){trueThree=body.true_three_phase;savedMethod=body.transport;savedHost=body.host;savedChannel=body.switch_id;savedPassword=body.transport===3?body.password||savedPassword:'';}state.phase_mqtt_allowed=allowed;return {ok:true};}if(body.pin!=='4040')throw Error('Passwort falsch');if(body.action==='authorize')return {ok:true};return {parameters:[],phase_mqtt_allowed:allowed,phase_mqtt_ready:true,settings:{mqtt_prefix:'my-teenet',phase_mqtt_true_three_phase:trueThree,phase_output_transport:savedMethod,phase_output_host:savedHost,phase_output_switch_id:savedChannel,phase_output_password_set:!!savedPassword},phase_output_base_ready:true,phase_output_mqtt_ready:savedMethod===0};},run:async work=>work()};
el('config-form').elements={phase_actuator_type:{value:'0'},phase_feedback_count:{value:'1',dispatchEvent(){}},phase_feedback_closed_is_single:{value:'false'},phase_feedback_enabled:{checked:true}};c.update=()=>c.updatePhaseMqtt();vm.createContext(c);vm.runInContext(src.slice(src.indexOf(' let phaseMqttDraft='),src.indexOf(' function makeRow(')),c);
(async()=>{
 c.openPhaseAccess();
 assert(el('phase-no-feedback-unlock-dialog').open,'Selecting no feedback requires its own warning/PIN even when expert PIN is in memory');
 el('expert-phase-mqtt-enabled').checked=true;await c.changePhaseMqtt();assert.equal(requests.length,0);
 c.cancelPhaseAccess();assert(!el('phase-no-feedback-unlock-dialog').open);
 c.openPhaseAccess();el('phase-no-feedback-pin').value='0000';await c.unlockPhaseAccess();
 assert(el('phase-no-feedback-unlock-dialog').open);assert.match(el('phase-no-feedback-unlock-error').textContent,/falsch/);
 assert(!requests.some(r=>r.enabled===true));
 el('phase-no-feedback-pin').value='4040';await c.unlockPhaseAccess();
 assert(!el('phase-no-feedback-unlock-dialog').open);assert.equal(el('phase-no-feedback-pin').value,'');
 assert.equal(requests.at(-1).action,'authorize','Repeated PIN uses the lightweight authorization action');
 assert.match(el('expert-phase-output-message').textContent,/Ansteuerung einrichten/);
 assert.equal(el('phase-setup-message').textContent,'','Expert messages stay in the expert card');
 assert(!requests.some(r=>r.enabled===true),'Unlocking only opens setup, never activates');requests=[];
 // The expert selector uses the same model list and separates connection/authentication.
 el('expert-phase-output-connection').options=[{value:'http'},{value:'modbus'}];
 el('expert-phase-output-actuator').value='1';
 for(const model of ['0','1','2','3','4','5','6','7','8','9','10','11','12','addon']){
  el('expert-phase-output-model').value=model;el('expert-phase-output-connection').value='modbus';
  el('expert-phase-output-auth').value='0';c.normalizePhaseShelly();
  const supported=['4','12'].includes(model),modbus=el('expert-phase-output-connection').options[1];
  assert.equal(modbus.hidden,!supported);assert.equal(modbus.disabled,!supported);
  assert.equal(el('expert-phase-output-transport').value,supported?'1':'2');
  assert.equal(el('expert-phase-output-channel').value,model==='addon'?'100':'0');
  el('expert-phase-output-connection').value='http';el('expert-phase-output-auth').value='1';c.normalizePhaseShelly();
  assert.equal(el('expert-phase-output-transport').value,'3');assert(!el('expert-phase-output-password-label').hidden);
 }
 const html=fs.readFileSync('main/dashboard.html','utf8');
 const normalModels=html.match(/<select name="phase_shelly_model" data-number>(.*?)<\/select>/)[1];
 assert(html.includes('<select id="expert-phase-output-model">'+normalModels),'Normal and expert modes list the same Shelly models');
 await c.scanPhaseShelly();assert(!el('expert-phase-output-found').hidden);
 assert.match(el('expert-phase-output-found').children[1].textContent,/Passwortschutz/);
 el('expert-phase-output-found').value='0';c.selectPhaseShelly();
 assert.equal(el('expert-phase-output-host').value,'192.168.1.88');assert.equal(el('expert-phase-output-model').value,'0');
 assert.equal(el('expert-phase-output-transport').value,'3','Search applies password protection using the same Shelly setup fields');
 el('expert-phase-output-host').value='';
 el('expert-phase-output-actuator').value='0';c.normalizePhaseShelly();
 assert.equal(el('expert-phase-output-transport').value,'0');assert(el('expert-phase-output-shelly').hidden);

 state.reboot_required=true;state.phase_output_setup_allowed=true;state.phase_output_mqtt_ready=false;c.update();
 assert(!el('expert-phase-mqtt-enabled').disabled,'An existing pending restart must not block saving the protected setup');
 assert.match(el('expert-phase-mqtt-status').textContent,/verbindet sich nach dem Neustart/);
 state.reboot_required=false;delete state.phase_output_setup_allowed;delete state.phase_output_mqtt_ready;
 el('expert-phase-mqtt-contactor').value='false';c.update();el('expert-phase-mqtt-topic').value='my-teenet/output/single_phase';
 el('expert-phase-mqtt-enabled').checked=true;await c.changePhaseMqtt();assert(el('phase-mqtt-confirm-dialog').open);assert(!el('phase-mqtt-warning-one').hidden);
 el('phase-mqtt-repeat-pin').value='4040';await c.activatePhaseMqtt();assert.equal(requests.length,0,'Password cannot bypass warning stages');
 c.cancelPhaseMqtt();assert(!el('phase-mqtt-confirm-dialog').open&&el('phase-mqtt-repeat-pin').value==='');assert.equal(requests.length,0);
 el('expert-phase-mqtt-enabled').checked=true;await c.changePhaseMqtt();c.nextPhaseMqttStep();assert(!el('phase-mqtt-warning-two').hidden);c.cancelPhaseMqtt();assert.equal(requests.length,0);
 el('expert-phase-mqtt-enabled').checked=true;await c.changePhaseMqtt();c.nextPhaseMqttStep();c.nextPhaseMqttStep();assert(!el('phase-mqtt-password-step').hidden);assert.equal(requests.length,0);
 await c.activatePhaseMqtt();assert.equal(requests.length,0,'Missing repeated password blocks activation');
 el('phase-mqtt-repeat-pin').value='4040';await c.activatePhaseMqtt();
 assert.equal(requests[0].action,'mqtt_phase_output');assert.equal(requests[0].repeat_pin,'4040');assert.equal(requests[0].topic,'my-teenet/output/single_phase');assert(requests[0].confirm_risk&&requests[0].confirm_no_feedback);
 assert(!requests.some(request=>request.action==='read'),'Phase activation must not fetch the large expert snapshot again');
 assert.equal(requests[0].true_three_phase,false);
 assert(el('expert-phase-mqtt-contactor').disabled,'Active output locks contactor polarity');
 assert.equal(c.snapshot.phase_mqtt_allowed,true);assert.equal(el('phase-mqtt-repeat-pin').value,'');
 state.enabled=true;c.update();assert(!el('expert-phase-mqtt-enabled').disabled,'Revoking remains possible during charging');
 requests=[];el('expert-phase-mqtt-enabled').checked=false;await c.changePhaseMqtt();assert.equal(requests[0].enabled,false);assert.equal(requests[0].repeat_pin,'');
 c.update();assert(el('expert-phase-mqtt-enabled').disabled,'Enabling while charging is locked');
 assert(el('expert-phase-mqtt-contactor').disabled,'Changing polarity while charging is locked');
 state.enabled=false;c.update();assert(!el('expert-phase-mqtt-contactor').disabled);
 el('expert-phase-mqtt-contactor').value='true';c.update();
 requests=[];el('expert-phase-mqtt-enabled').checked=true;await c.changePhaseMqtt();
 c.nextPhaseMqttStep();c.nextPhaseMqttStep();el('phase-mqtt-repeat-pin').value='4040';
 el('expert-phase-mqtt-contactor').value='false';await c.activatePhaseMqtt();
 assert.equal(requests[0].true_three_phase,true,'Confirmed mapping is frozen at the first warning');
 assert.equal(el('expert-phase-mqtt-contactor').value,'true','Stored mapping is shown after activation');
 for(const method of [1,2,3]){
  el('expert-phase-mqtt-enabled').checked=false;await c.changePhaseMqtt();
  el('expert-phase-output-model').value=method===1?'4':'0';el('expert-phase-output-transport').value=String(method);el('expert-phase-output-host').value='192.168.1.88';el('expert-phase-output-channel').value='0';
  if(method===3)el('expert-phase-output-password').value='synthetic-secret';c.snapshot.phase_output_mqtt_ready=false;c.update();assert(!el('expert-phase-mqtt-enabled').disabled,'Direct Shelly does not require MQTT');
  assert(el('expert-phase-mqtt-topic').closest('label').hidden&&el('expert-phase-mqtt-broker-note').hidden);
  requests=[];el('expert-phase-mqtt-enabled').checked=true;await c.changePhaseMqtt();c.nextPhaseMqttStep();c.nextPhaseMqttStep();
  assert.match(el('phase-mqtt-access-warning').textContent,/Shelly-Zugriff/);el('phase-mqtt-repeat-pin').value='4040';
  el('expert-phase-output-host').value='192.168.1.99';await c.activatePhaseMqtt();
  assert.equal(requests[0].shelly_model,method===1?4:0);assert.equal(requests[0].transport,method);assert.equal(requests[0].host,'192.168.1.88','Endpoint frozen at warning');assert.equal(requests[0].switch_id,0);
  if(method===3)assert.equal(requests[0].password,'synthetic-secret');else assert(!('password' in requests[0]));assert(!('phase_shelly_password' in requests[0]));assert.equal(el('expert-phase-output-transport').value,String(method));
 }
 el('expert-phase-mqtt-enabled').checked=false;await c.changePhaseMqtt();
 c.snapshot.phase_mqtt_allowed=false;state.phase_mqtt_allowed=false;state.phase_output_meter_ready=false;state.wallbox_meter_type='xemex';state.xemex_coils=1;c.update();assert(el('expert-phase-mqtt-enabled').disabled);assert.equal(el('expert-phase-mqtt-status').textContent,'Xemex: mindestens L1 und L2 messen.');
state.phase_output_meter_ready=state.phase_output_base_ready=true;state.phase_output_measured_phases=state.xemex_coils=2;c.update();assert(!el('expert-phase-mqtt-enabled').disabled);assert.match(el('expert-phase-mqtt-status').textContent,/L3 wird geschätzt/);
 state.phase_output_meter_ready=true;c.snapshot.phase_mqtt_allowed=true;state.phase_mqtt_allowed=true;
 vm.runInContext("pin='';phaseAccess=false;snapshot=null;",c);c.configBaseline={phase_mqtt_output_enabled:true,phase_output_transport:3,phase_output_host:'192.168.1.88',phase_output_password_set:true};
 c.update();assert(!el('expert-phase-mqtt-enabled').disabled,'Active output can be revoked with locked expert area');
 requests=[];el('expert-phase-mqtt-enabled').checked=false;await c.changePhaseMqtt();assert.equal(requests[0].enabled,false);assert.equal(requests.length,1,'Revocation does not attempt privileged read with empty PIN');
 state.phase_mqtt_allowed=false;el('phase-feedback-choice').value='2';await c.selectFeedback();assert.equal(el('config-form').elements.phase_feedback_count.value,'2');assert.equal(el('config-form').elements.phase_actuator_type.value,'0');assert(el('config-form').elements.phase_feedback_enabled.checked);
 assert(!fs.readFileSync('main/app.c','utf8').includes('phase_mqtt_command_locked'),'No incoming phase parser remains');
 assert.equal(fs.readFileSync('main/dashboard.html','utf8').split('Schalttopic am Broker nur für TeeNet zum Schreiben freigeben.').length-1,2,'Broker write-permission warning is shown in settings and activation');
 console.log('PASS: local phase setup: lightweight password gate, wrong/cancel isolation, no activation on unlock; expert UI NC/NO dropdown, concise warnings, stopped/active locks, two cancellable warnings and masked PIN, revocation and no inbound parser');
})().catch(error=>{console.error(error);process.exitCode=1;});
