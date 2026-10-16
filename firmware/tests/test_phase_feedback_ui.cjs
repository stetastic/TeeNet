const fs=require('node:fs'),vm=require('node:vm'),assert=require('node:assert/strict');
const source=fs.readFileSync('tools/phase_shelly_ui.js','utf8'),dashboard=fs.readFileSync('main/dashboard.js','utf8');
const nodes={},f={phase_actuator_type:{value:'1'}},feedback={value:'2'};
const state={phase_switch_enabled:true,phase_actuator_type:1,phase_feedback_count:2,
 phase_contact1_closed:true,phase_contact2_closed:false,phase_feedback:3};
const context={state,f,feedback,Number,$:id=>nodes[id]??={hidden:false,textContent:''}};
vm.createContext(context);
vm.runInContext(source.slice(source.indexOf(' function updateLive(){'),source.indexOf(' function render(result){')),context);
context.updateLive();assert.match(nodes['phase-contact-status'].textContent,/Kontakt 1: geschlossen · Kontakt 2: offen · Dreiphasig bestätigt/);
Object.assign(state,{phase_contact1_closed:false,phase_contact2_closed:true,phase_feedback:1});
context.updateLive();assert.match(nodes['phase-contact-status'].textContent,/Kontakt 1: offen · Kontakt 2: geschlossen · Einphasig bestätigt/);
Object.assign(state,{phase_contact1_closed:null,phase_contact2_closed:null,phase_feedback:0});
context.updateLive();assert.match(nodes['phase-contact-status'].textContent,/Kontakt 1: unbekannt · Kontakt 2: unbekannt · Schützstellung nicht bestätigt/);
assert.equal(f.phase_actuator_type.value,'1');assert.equal(feedback.value,'2');
f.phase_actuator_type.value='0';context.updateLive();assert(nodes['phase-contact-status'].hidden,'Hide live contacts when editing a different actuator');
assert(dashboard.slice(dashboard.indexOf('function writes(){'),dashboard.indexOf('function showConnection')).includes('window.teenetShellyPhase?.updateLive();'));
assert(dashboard.replace(/\r\n/g,'\n').includes(source.replace(/\r\n/g,'\n')));
console.log('PASS: live phase contacts refresh in both directions, unknown data shown, unsaved configuration untouched, dashboard poll hook and mirrored fragment');
