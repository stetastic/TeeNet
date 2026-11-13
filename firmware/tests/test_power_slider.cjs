const assert=require('node:assert/strict');
const fs=require('node:fs');
const vm=require('node:vm');
const source=fs.readFileSync('main/dashboard.js','utf8');
const start=source.indexOf('function powerFactor('),end=source.indexOf('async function action(',start);
assert.ok(start>=0&&end>start);
const slider={value:'0'};
const state={single_power_per_amp_kw:.23,min_charge_a:7.8,max_charge_a:16,
 phase_switch_enabled:false,charge_phases:3,manual_phases:3};
const context=vm.createContext({state,$:id=>id==='current'?slider:null,format:n=>String(n)});
vm.runInContext(source.slice(start,end)+';globalThis.api={powerSteps,phaseForPower,selectedCurrent};',context);
const read=()=>Array.from(context.api.powerSteps());
assert.deepEqual(read(),[0,5.6,6,6.5,7,7.5,8,8.5,9,9.5,10,10.5,11]);
state.phase_switch_enabled=true;
assert.deepEqual(read(),[0,2,2.5,3,3.5,5.6,6,6.5,7,7.5,8,8.5,9,9.5,10,10.5,11]);
slider.value='1';assert.equal(context.api.phaseForPower(2),1);
assert.ok(context.api.selectedCurrent()>=7.8&&context.api.selectedCurrent()<9);
slider.value='5';assert.equal(context.api.phaseForPower(6),3);
assert.ok(context.api.selectedCurrent()>=7.8&&Math.abs(context.api.selectedCurrent()-5.6/.69)<.001);
slider.value='0';assert.equal(context.api.selectedCurrent(),0);
console.log('PASS: 3-phase slider unchanged, 1-phase steps added, 0.5 kW commands map to phases');

state.phase_switch_enabled=false;state.fixed_charge_phases=1;state.charge_phases=state.manual_phases=1;
assert.deepEqual(read(),[0,2,2.5,3,3.5]);
slider.value='1';assert.equal(context.api.phaseForPower(2),1);assert.ok(Math.abs(context.api.selectedCurrent()-2/.23)<.001);
state.fixed_charge_phases=3;state.charge_phases=state.manual_phases=3;
assert.deepEqual(read(),[0,5.6,6,6.5,7,7.5,8,8.5,9,9.5,10,10.5,11]);
console.log('PASS: fixed single-phase slider excludes 3-phase powers and preserves normal defaults');
state.min_charge_a=7.5;state.single_power_per_amp_kw=.2366;state.three_power_per_amp_kw=.7069;
assert(read().includes(5.5));slider.value=String(read().indexOf(5.5));assert(Math.abs(context.api.selectedCurrent()-5.5/.7069)<1e-7,'three-phase conversion must use the sum, not three times L1');
slider.value=String(read().indexOf(11));assert(Math.abs(context.api.selectedCurrent()-11/.7069)<1e-7);
console.log('PASS: configured 5.5/11-kW steps use measured unequal phase voltages');

state.min_charge_a=8.7;state.three_power_per_amp_kw=.708;
assert(read().includes(6.2));assert(!read().includes(6));assert(!read().includes(5.5));slider.value=String(read().indexOf(6.2));
assert(context.api.selectedCurrent()>=8.7,'displayed step must respect the configured current floor');
console.log('PASS: nominal 6-kW step with measured voltages retains safe current floor');
