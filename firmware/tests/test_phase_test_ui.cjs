const fs=require('node:fs'),vm=require('node:vm'),assert=require('node:assert/strict');
const nodes={},calls=[];const last=()=>JSON.parse(JSON.stringify(calls.at(-1)));
function el(id){return nodes[id]??={hidden:true,disabled:false,open:false,value:'',textContent:'',dataset:{},listeners:{},addEventListener(k,f){this.listeners[k]=f;},showModal(){this.open=true;},close(){this.open=false;}};}
const state={phase_test_ready:false},window={};
const c={state,window,$:el,online:true,initialized:true,busy:false,toast(){},action:fn=>fn(),pollStatus:async()=>{},api:async(path,body)=>{calls.push({path,body});if(body.pin&&body.pin!=='4040')throw Error('Passwort falsch');return {ok:true};}};
vm.createContext(c);const src=fs.readFileSync('tools/phase_test_ui.js','utf8');vm.runInContext(src,c);
(async()=>{
 assert(!el('phase-test-single').disabled);el('phase-test-single').listeners.click();assert(el('phase-test-dialog').open&&el('phase-test-confirm').disabled);el('phase-test-cancel').listeners.click();assert(!calls.length);
 state.phase_test_ready=true;window.teenetPhaseTest.update();assert(!el('phase-test-three').disabled);
 el('functions-unsaved').hidden=false;el('functions-unsaved').dataset.kind='unsaved';window.teenetPhaseTest.update();assert(!el('phase-test-single').disabled);el('phase-test-single').listeners.click();assert(el('phase-test-dialog').open&&el('phase-test-confirm').disabled);el('phase-test-cancel').listeners.click();
 el('functions-unsaved').hidden=true;window.teenetPhaseTest.update();el('phase-test-single').listeners.click();assert(el('phase-test-dialog').open&&el('phase-test-confirm-text').textContent.includes('Einphasig'));el('phase-test-cancel').listeners.click();assert(!el('phase-test-dialog').open&&!calls.length);
 el('phase-test-three').listeners.click();el('phase-test-pin').value='0000';await el('phase-test-confirm').listeners.click();assert(el('phase-test-dialog').open&&el('phase-test-error').textContent==='Passwort falsch'&&el('phase-test-pin').value==='');
 el('phase-test-pin').value='4040';await el('phase-test-confirm').listeners.click();assert(!el('phase-test-dialog').open);assert.deepEqual(last(),{path:'/api/phase-test',body:{phases:3,pin:'4040',confirm_risk:true}});
 state.phase_test_active=true;state.phase_test_goal=3;window.teenetPhaseTest.update();assert(!el('phase-test-stop').hidden);await el('phase-test-stop').listeners.click();assert.deepEqual(last(),{path:'/api/phase-test',body:{stop:true}});
 assert(fs.readFileSync('main/dashboard.js','utf8').replace(/\r\n/g,'\n').includes(src.replace(/\r\n/g,'\n')));
 console.log('PASS: hardware-test UI: stopped readiness, unsaved draft lock, warning/PIN, cancellation, password failure, single confirmed request and always-available test stop');
})().catch(e=>{console.error(e);process.exitCode=1;});
