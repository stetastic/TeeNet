const fs=require('node:fs'),vm=require('node:vm'),assert=require('node:assert/strict');
const js=fs.readFileSync('main/dashboard.js','utf8'),html=fs.readFileSync('main/dashboard.html','utf8');
const expert=html.indexOf('id="expert-content"'),end=html.indexOf('id="expert-message"');
for(const id of ['phase-hardware-test','expert-phase-mqtt']){
 const at=html.indexOf(`id="${id}"`);assert(at>expert&&at<end,`${id} must be inside the locked expert content`);
}
assert(!html.includes('>Automatische Phasenumschaltung</label>'));
assert(!html.includes('Ohne Rückmeldung · geschützt'),'Protected no-feedback control must not appear in the ordinary actuator dropdown');
const feedback=html.match(/id="phase-feedback-choice">(.*?)<\/select>/)[1];
assert(!feedback.includes('value="0"'));assert(feedback.includes('1-single'));
assert(!js.includes("if(el('phase-feedback-choice').value==='0'"),'Expert preview must not intercept normal form saves');
assert(js.includes(fs.readFileSync('tools/expert_ui.js','utf8').trim()));
const details={tagName:'DETAILS',open:false,parentElement:null};let focused=false,reported=false,notices=0;
function input({name='phase_shelly_host',invalid=false,expert=false,hidden=false,disabled=false}={}){
 return {name,disabled,parentElement:details,validationMessage:'Adresse fehlt',
 closest(selector){return selector==='#expert-settings'?(expert?{}:null):selector==='[hidden]'?(hidden?{}:null):{firstChild:{textContent:'Shelly-Adresse'}};},
 checkValidity(){return !invalid;},focus(){focused=true;},reportValidity(){reported=true;}};
}
const elements=[input({invalid:true,expert:true}),input({invalid:true,hidden:true}),input({invalid:true,disabled:true}),input()];
const ctx={configSaveError:'',$:()=>({elements}),updateUnsavedFunctions(){notices++;}};vm.createContext(ctx);
vm.runInContext(js.slice(js.indexOf('function validateConfigForm(){'),js.indexOf("$('config-form').addEventListener('submit'")),ctx);
assert(ctx.validateConfigForm());assert.equal(notices,0);
elements.push(input({invalid:true}));assert(!ctx.validateConfigForm());assert(details.open&&focused&&reported);assert.match(ctx.configSaveError,/Shelly-Adresse: Adresse fehlt/);assert.equal(notices,1);
console.log('PASS: expert-only test and no-feedback option, combined contact mapping, independent saves, visible field error and opened settings card');
