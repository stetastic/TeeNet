const fs=require('node:fs'),vm=require('node:vm'),assert=require('node:assert/strict');
const src=fs.readFileSync('tools/expert_ui.js','utf8');
const functions=src.slice(src.indexOf(' function chargingActive()'),src.indexOf(' function makeRow('));
const nodes={};const classes={toggle(){}};
const el=id=>nodes[id]||=({disabled:false,textContent:'',closest(){return this.label??={hidden:false};},classList:classes,querySelectorAll:()=>[],querySelector:()=>null,parentElement:{classList:classes}});
const row=()=>({input:{disabled:false,value:30},reset:{disabled:false},node:{classList:classes},standard:30,saved:30,key:'PV_START'});
const c={updateGPIO(){},online:true,initialized:true,busy:false,uploading:false,configSaving:false,pin:'4040',working:false,
 state:{enabled:false,evcc_enabled:false,phase_switching:false,meter_ok:true,actual_a:[0,0,0]},rows:[row()],snapshot:null,configBaseline:{},savedModified:false,lastMarkerState:null,el,
 same:(a,b)=>a===b,value:r=>Number(r.input.value),changed:r=>Number(r.input.value)!==r.saved};
vm.createContext(c);vm.runInContext(functions,c);
function check(enabled){c.update();assert.equal(c.rows[0].input.disabled,!enabled);assert.equal(c.rows[0].reset.disabled,!enabled);assert.equal(el('expert-defaults').disabled,!enabled);assert.equal(el('expert-save').disabled,true);assert.equal(el('expert-unsaved').hidden,true);}
check(true);
for(const [key,value] of [['enabled',true],['evcc_enabled',true],['phase_switching',true],['meter_ok',false],['actual_a',[0,2,0]],['actual_a',[null,0,0]],['actual_a',[NaN,0,0]],['actual_a',[0,0]],['actual_a',[0,0,-1]]]){
 const original=c.state[key];c.state[key]=value;check(false);c.state[key]=original;check(true);
}
for(const flag of ['busy','uploading','configSaving','working']){c[flag]=true;check(false);c[flag]=false;}
c.online=false;check(false);c.online=true;c.initialized=false;check(false);c.initialized=true;
c.rows[0].input.value=20;c.update();assert.equal(el('expert-save').disabled,false);assert.equal(el('expert-unsaved').hidden,false,'floating save prompt appears immediately');assert.equal(el('expert-unsaved-save').disabled,false);assert.match(el('expert-unsaved-text').textContent,/Eine Änderung/);
c.state.enabled=true;c.update();assert.equal(el('expert-save').disabled,true);assert.equal(c.rows[0].input.value,20,'charging preserves unsaved draft');
assert.equal(el('expert-unsaved').hidden,false);assert.equal(el('expert-unsaved-save').disabled,true,'save prompt stays visible but locked while charging');
assert.equal(el('expert-stop').hidden,false,'direct stop appears during charging');
c.state.enabled=false;c.update();assert.equal(el('expert-save').disabled,false,'stopping enables draft again');
assert.equal(el('expert-stop').hidden,true,'direct stop disappears after stopping');
c.rows[0].input.value=30;
c.state.evcc_test_enabled=true;check(false);assert.equal(el('expert-stop').hidden,false);
c.state.evcc_local_stop=true;check(true);c.state.evcc_test_enabled=false;
c.state.phase_switch_enabled=true;c.state.phase_state='schalten';check(false);c.state.phase_state='bereit';check(true);
assert(src.includes("if(!editAllowed()){update();message("),'save checks stopped state');
assert(fs.readFileSync('main/expert_http.inc','utf8').includes('if(!stopped)'),'backend independently rejects active charging writes');
assert(fs.readFileSync('main/dashboard.js','utf8').replace(/\r\n/g,'\n').includes(src.replace(/\r\n/g,'\n')),'embedded UI matches editable source');
assert(!fs.readFileSync('main/dashboard.html','utf8').includes('id="expert-search"'),'parameter search removed');
let request;
c.action=fn=>fn();c.rememberChargeMode=()=>{};c.message=()=>{};
c.api=async(path,body)=>{request={path,body};};c.pollStatus=async()=>{c.state.enabled=false;};
c.state.enabled=true;
(async()=>{await c.stopExpertCharging();assert.equal(request.path,'/api/control');assert.equal(request.body.enabled,false);assert.equal(request.body.mode,'off');assert.equal(c.state.enabled,false);
 console.log('PASS: editing lock, evcc/phase switch, missing readings and direct stop; stopped charging enables editing');
})().catch(error=>{console.error(error);process.exitCode=1;});

const html=fs.readFileSync('main/dashboard.html','utf8');
assert(html.includes('<details class="expert-section"><summary>Regelparameter</summary>'),'rule parameters initially collapsed');
assert(html.includes('class="expert-warning"'),'warning within unlocked expert content');
assert(html.indexOf('class="expert-warning"')>html.indexOf('id="expert-content" hidden'));
assert(html.includes('unerwarteten Fehlfunktionen'));
