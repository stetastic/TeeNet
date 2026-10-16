const fs=require('node:fs'),vm=require('node:vm'),assert=require('node:assert/strict');
const nodes={};function el(id){return nodes[id]??={hidden:false,disabled:false,children:[],listeners:{},value:'',textContent:'',addEventListener(k,f){this.listeners[k]=f;},replaceChildren(){this.children=[];},append(child){this.children.push(child);}};}
const parent=()=>({hidden:false});
function field(value,checked=false){const label=parent();return {value,checked,required:false,listeners:{},addEventListener(k,fn){this.listeners[k]=fn;},closest(){return label;},dispatchEvent(){this.changed=true;}};}
const f={phase_feedback_count:field('2'),phase_feedback_closed_is_single:field('false'),phase_shelly_model:field('0'),phase_shelly_pulse:field('true'),phase_shelly_true_three_phase:field('false'),phase_actuator_type:field('1'),phase_switch_enabled:field('',true),relay_active_low:field('false'),relay1_mode:field('0'),phase_feedback_enabled:field('',false),phase_shelly_host:field(''),phase_shelly_password:field('')};
el('phase-feedback-choice').value='2';el('phase-feedback-choice').options=[{value:'1'},{value:'1-single'},{value:'2'}];el('phase-shelly-connection').value='1';el('phase-shelly-connection').options=[{value:'1'},{value:'3'}];el('config-form').elements=f;let requests=[];
const context={window:{},$:el,online:true,busy:false,state:{phase_actuator_type:1,phase_switch_enabled:true,phase_feedback:1,phase_contact1_closed:false,phase_contact2_closed:true,phase_feedback_count:2},
 document:{querySelector(){return el('phase-feedback-title');},createElement(){return {value:'',textContent:'',disabled:false};}},Event:function(){},clearTimeout(){},setTimeout(){throw Error('Unexpected polling in completed synthetic search');},
 api:async(path,body)=>{requests.push({path,body});return {family:'shelly_switch',running:false,progress:100,devices:[{host:'192.168.1.88',name:'Protected Pro 1',protected:true,compatible:true},{host:'192.168.1.89',name:'Other Shelly',compatible:false}]};}};
vm.runInNewContext(fs.readFileSync('tools/phase_shelly_ui.js','utf8'),context);
(async()=>{
 assert(f.phase_feedback_enabled.checked);assert.equal(f.relay1_mode.value,'3');assert(f.phase_shelly_host.required);
 assert(f.phase_feedback_enabled.closest('label').hidden);
 assert.equal(el('phase-feedback-heading').textContent,'Rückmeldung über Shelly');assert.match(el('phase-feedback-description').textContent,/Shelly-Eingänge/);assert(!el('phase-feedback-pin-note').textContent.includes('GPIO'));assert(el('phase-connection-note').hidden);
 assert.equal(el('phase-contact-status').textContent,'Kontakt 1: offen · Kontakt 2: geschlossen · Einphasig bestätigt');
 context.state.phase_contact1_closed=context.state.phase_contact2_closed=null;context.state.phase_feedback=0;
 context.window.teenetShellyPhase.update();assert.equal(el('phase-contact-status').textContent,'Kontakt 1: unbekannt · Kontakt 2: unbekannt · Schützstellung nicht bestätigt');
 await el('phase-shelly-scan').listeners.click();assert.equal(requests[0].path,'/api/meters/scan');assert.equal(requests[0].body.family,'shelly_switch');
 const choices=el('phase-shelly-found').children;assert(choices[1].textContent.includes('Passwortschutz'));assert(!choices[1].disabled&&choices[2].disabled);
 el('phase-shelly-found').value='0';el('phase-shelly-found').listeners.change();assert.equal(f.phase_shelly_host.value,'192.168.1.88');assert(f.phase_shelly_host.changed,'Selection notifies normal unsaved-settings state');
 f.phase_switch_enabled.checked=false;context.window.teenetShellyPhase.update();assert.equal(f.relay1_mode.value,'0');assert(!f.phase_shelly_host.required);
 f.phase_actuator_type.value='0';context.window.teenetShellyPhase.update();assert(el('phase-shelly-settings').hidden);
 assert.equal(el('phase-feedback-heading').textContent,'Rückmeldung am ESP');assert.match(el('phase-feedback-description').textContent,/ESP-Eingänge/);
 assert(el('phase-connection-note').innerHTML.includes('3,3 V / High schaltet ein'));
 assert(!fs.readFileSync('main/dashboard.html','utf8').includes('name="relay_active_low"'));assert.match(el('phase-feedback-pin-note').textContent,/GPIO13 gegen GND geschlossen: dreiphasig.*GPIO14 gegen GND geschlossen: einphasig/);
 context.state.phase_actuator_type=0;context.state.phase_contact1_closed=true;context.state.phase_contact2_closed=false;context.state.phase_feedback=3;
 context.window.teenetShellyPhase.update();assert(!el('phase-contact-status').hidden);assert.equal(el('phase-contact-status').textContent,'Kontakt 1: geschlossen · Kontakt 2: offen · Dreiphasig bestätigt');
 context.state.phase_contact2_closed=true;context.state.phase_feedback=0;context.window.teenetShellyPhase.update();assert(el('phase-contact-status').textContent.includes('Kontakt 1: geschlossen · Kontakt 2: geschlossen · Schützstellung nicht bestätigt'));
 context.state.phase_feedback_count=1;context.window.teenetShellyPhase.update();assert(!el('phase-contact-status').textContent.includes('Kontakt 2'));
 assert(fs.readFileSync('main/dashboard.js','utf8').includes(fs.readFileSync('tools/phase_shelly_ui.js','utf8')));
 assert(!fs.readFileSync('main/dashboard.html','utf8').includes('id="save-phase-shelly-password"'),'Shelly password uses the shared settings save, without a duplicate submit button');
 f.phase_switch_enabled.checked=true;f.phase_shelly_model.value='0';el('phase-shelly-connection').value='3';el('phase-shelly-connection').listeners.change({target:el('phase-shelly-connection')});context.window.teenetShellyPhase.update();assert.equal(f.phase_actuator_type.value,'3');assert.equal(f.phase_shelly_model.value,'4');assert.equal(f.phase_feedback_count.value,'1');assert.equal(f.phase_shelly_pulse.value,'false');assert(el('phase-shelly-auth-settings').hidden);assert(f.phase_shelly_password.disabled&&!f.phase_shelly_password.required);assert.match(el('phase-choice-note').textContent,/Modbus TCP gewählt/);assert(!el('phase-choice-note').hidden);
 f.phase_shelly_model.options=[0,1,2,3,4].map(value=>({value:String(value),disabled:true}));f.phase_shelly_model.value='0';f.phase_shelly_model.listeners.change();context.window.teenetShellyPhase.update();assert.equal(f.phase_actuator_type.value,'1');assert.equal(el('phase-shelly-connection').value,'1');assert.equal(f.phase_shelly_model.value,'0');assert(f.phase_shelly_model.options.every(o=>!o.disabled));
 f.phase_actuator_type.value='1';el('phase-shelly-auth').value='1';context.window.teenetShellyPhase.update();assert(!f.phase_shelly_password.disabled&&f.phase_shelly_password.required);
 f.phase_actuator_type.value='2';context.state.phase_mqtt_allowed=true;context.window.teenetShellyPhase.update();assert(!el('expert-phase-mqtt').hidden);assert(el('phase-feedback-pin-note').hidden);assert(el('phase-actuator-label').hidden);assert(!f.phase_shelly_host.required);
 assert(el('phase-feedback-settings').hidden,'No-feedback output must hide the complete normal feedback section');
 const html=fs.readFileSync('main/dashboard.html','utf8');assert(!html.includes('>Relaisfunktion<'));assert(html.includes('1 Kontakt · geschlossen = 3-phasig'));assert(html.includes('<option value="1">Shelly</option>'));assert(html.includes('Shelly 1 Gen4 · HTTP oder Modbus TCP'));assert(html.includes('Modbus TCP · nur Shelly 1 Gen4'));
 f.phase_actuator_type.value='1';f.phase_feedback_count.value=el('phase-feedback-choice').value='1';f.phase_feedback_closed_is_single.value='true';context.window.teenetShellyPhase.update();assert(el('phase-feedback-pin-note').textContent.includes('geschlossen: einphasig'));assert(el('phase-feedback-pin-note').textContent.includes('Offen: dreiphasig'));
 f.phase_feedback_count.value=el('phase-feedback-choice').value='2';context.window.teenetShellyPhase.update();assert.equal(f.phase_feedback_closed_is_single.value,'true','Rendering must not rewrite loaded settings');f.phase_shelly_pulse.value='true';f.phase_shelly_pulse.listeners.change();assert.equal(f.phase_feedback_closed_is_single.value,'false');assert(el('phase-feedback-single-mapping').hidden);
 context.state.phase_actuator_type=1;context.state.phase_switch_enabled=true;
 context.configBaseline=Object.fromEntries(['phase_feedback_count','phase_feedback_closed_is_single','phase_shelly_model','phase_shelly_pulse','phase_shelly_true_three_phase','phase_shelly_host'].map(name=>[name,f[name].value]));
 f.phase_feedback_closed_is_single.value='true';context.window.teenetShellyPhase.updateLive();assert.equal(el('phase-contact-status').textContent,'Neue Zuordnung noch nicht gespeichert.');
 f.phase_feedback_closed_is_single.value='false';context.state.reboot_required=true;context.window.teenetShellyPhase.updateLive();assert.equal(el('phase-contact-status').textContent,'Rückmeldung nach dem Neustart prüfen.');
 context.state.reboot_required=false;context.window.teenetShellyPhase.updateLive();assert(el('phase-contact-status').textContent.includes('Kontakt 1:'));
 console.log('PASS: Shelly setup UI: independent scan, protected selection, inactive profile, obligatory feedback, normal dirty/save workflow and GPIO controls restored');
})().catch(error=>{console.error(error);process.exitCode=1;});
