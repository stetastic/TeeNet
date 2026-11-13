const fs=require('node:fs'),vm=require('node:vm'),assert=require('node:assert/strict');
const nodes={};function el(id){return nodes[id]??={hidden:false,disabled:false,children:[],listeners:{},value:'',textContent:'',addEventListener(k,f){this.listeners[k]=f;},replaceChildren(){this.children=[];},append(child){this.children.push(child);}};}
const parent=()=>({hidden:false});
function field(value,checked=false){const label=parent();return {value,checked,required:false,listeners:{},addEventListener(k,fn){this.listeners[k]=fn;},closest(){return label;},dispatchEvent(){this.changed=true;}};}
const f={phase_feedback_source:field('0'),phase_voltage_host:field(''),phase_voltage_protocol:field('0'),phase_no_feedback_acknowledged:field('',false),phase_feedback_count:field('2'),phase_feedback_closed_is_single:field('false'),phase_shelly_model:field('0'),phase_shelly_pulse:field('true'),phase_shelly_true_three_phase:field('false'),phase_actuator_type:field('1'),phase_switch_enabled:field('',true),relay_active_low:field('false'),relay1_mode:field('0'),phase_feedback_enabled:field('',false),phase_shelly_host:field(''),phase_shelly_password:field('')};
el('phase-feedback-choice').value='2';el('phase-feedback-choice').options=[{value:'1'},{value:'1-single'},{value:'2'}];el('phase-shelly-connection').value='1';el('phase-shelly-connection').options=[{value:'1'},{value:'3'}];el('config-form').elements=f;let requests=[];
const context={window:{addEventListener(){}},$:el,online:true,busy:false,state:{phase_actuator_type:1,phase_switch_enabled:true,phase_feedback:1,phase_contact1_closed:false,phase_contact2_closed:true,phase_feedback_count:2},
 document:{querySelector(){return el('phase-feedback-title');},createElement(){return {value:'',textContent:'',disabled:false};}},Event:function(){},clearTimeout(){},setTimeout(){throw Error('Unexpected polling in completed synthetic search');},
 api:async(path,body)=>{requests.push({path,body});return {family:'shelly_switch',running:false,progress:100,devices:[{host:'192.168.1.88',name:'Protected Pro 1',protected:true,compatible:true},{host:'192.168.1.89',name:'Other Shelly',compatible:false}]};}};
vm.runInNewContext(fs.readFileSync('tools/phase_shelly_ui.js','utf8'),context);
(async()=>{
 for(let model=0;model<13;model++){
  f.phase_shelly_model.value=String(model);context.window.teenetShellyPhase.update();
  assert.equal(el('phase-shelly-connection').options[1].disabled,![4,12].includes(model));
 }
 await el('phase-shelly-scan').listeners.click();assert.equal(requests[0].body.family,'shelly_switch');
 const choices=el('phase-shelly-found').children;assert(choices[1].textContent.includes('Passwortschutz'));assert(!choices[1].disabled&&choices[2].disabled);
 el('phase-shelly-found').value='0';el('phase-shelly-found').listeners.change();assert.equal(f.phase_shelly_host.value,'192.168.1.88');assert(f.phase_shelly_host.changed);
 f.phase_shelly_model.value='4';el('phase-shelly-connection').value='3';el('phase-shelly-connection').listeners.change({target:el('phase-shelly-connection')});
 assert.equal(f.phase_actuator_type.value,'3');assert.equal(el('phase-actuator-choice').value,'1');
 assert(f.phase_shelly_password.disabled&&!f.phase_shelly_password.required);
 const html=fs.readFileSync('main/dashboard.html','utf8'),options=html.split('id="phase-actuator-choice"')[1].split('</select>')[0];
 assert.equal((options.match(/>Shelly</g)||[]).length,1);assert(!options.includes('value="3"'));
 console.log('PASS: 13 Shelly model choices, independent protected discovery, correct transport limits and one native Shelly option');
})().catch(error=>{console.error(error);process.exitCode=1;});
