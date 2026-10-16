const fs=require('node:fs'),vm=require('node:vm'),assert=require('node:assert/strict');
const src=fs.readFileSync('main/dashboard.js','utf8');
const calls=[],messages=[];let submit;
const context=vm.createContext({state:{},Number,api:async(path,body)=>calls.push({path,body}),toast:t=>messages.push(t),pollStatus:async()=>{},dirty:true,selectedCurrent:()=>9.42,selectedPower:()=>6.5,phaseForPower:()=>3,action:fn=>fn(),$:id=>id==='control-form'?{addEventListener:(_,fn)=>submit=fn}:{value:'manual'}});
vm.runInContext(src.slice(src.indexOf('let preferredChargeMode='),src.indexOf('reasons.charge_ended=')),context);
vm.runInContext(src.slice(src.indexOf('function chargeButtonLabel('),src.indexOf('function render(){')),context);
vm.runInContext(src.slice(src.indexOf("$('control-form').addEventListener('submit'"),src.indexOf("$('unlock-control').addEventListener")),context);
async function press(state){context.state=state;calls.length=0;await submit({preventDefault(){}});await new Promise(resolve=>setImmediate(resolve));return calls.map(c=>JSON.parse(JSON.stringify(c.body)));}
(async()=>{
  assert.equal(context.chargeButtonLabel({enabled:false},true,false),'Gestoppt · Starten');
  assert.equal(context.chargeButtonLabel({enabled:true},true,true),'Lädt · Stoppen');
  assert.equal(context.chargeButtonLabel({enabled:true,mode:'pv'},true,false),'PV wartet · Stoppen');
  assert.equal(context.chargeButtonLabel({enabled:false},true,true),'Stoppt …');
  assert.equal(context.chargeButtonLabel({},false,false),'Verbindung fehlt');
  assert.equal(context.chargeButtonLabel({enabled:true,phase_state:'fehler',can_unlock:false},true,false),'Gestört · Stoppen');
  assert.deepEqual(await press({enabled:true,phase_state:'fehler',can_unlock:false}),[{enabled:false,mode:'off'}]);
  assert.deepEqual(await press({phase_test_active:true}),[{stop:true}]);
  assert.deepEqual(await press({enabled:true}),[{enabled:false,mode:'off'}]);
  assert.deepEqual(await press({enabled:false,feedback_ok:true,actual_a:[0,0,0]}),[{mode:'manual',current_a:9.42,manual_phases:3,enabled:true,restart:true}]);
  assert.deepEqual(await press({enabled:false,feedback_ok:true,actual_a:[5,5,5]}),[], 'A pending stop must not become an accidental restart');
  assert.deepEqual(await press({charge_stop_latched:true,can_unlock:false}),[]);
  assert.deepEqual(await press({charge_stop_latched:true,can_unlock:true}),[{enabled:false,restart:true}], 'Unlock leaves charging off');
  console.log('Single charging button: labels, stop, start, pending stop and unlock passed');
})().catch(e=>{console.error(e);process.exitCode=1;});
