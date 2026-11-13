const fs=require('node:fs'),vm=require('node:vm'),assert=require('node:assert/strict');
const source=fs.readFileSync('main/dashboard.js','utf8');
const context={state:{min_charge_a:7.8,max_charge_a:16,fixed_charge_phases:3,phase_switch_enabled:false,three_power_per_amp_kw:.70,single_power_per_amp_kw:.233333333},Math,Number};
vm.createContext(context);
vm.runInContext(source.slice(source.indexOf('function powerFactor('),source.indexOf('function selectedPower(')),context);
const steps=()=>Array.from(context.powerSteps());
assert.equal(steps()[1],5.6);assert(steps().includes(6));assert(!steps().includes(5.5));
for(const factor of [.66,.69,.70,.716,.75]){
 context.state.three_power_per_amp_kw=factor;
 const values=steps();assert(values[1]>=5.6);assert(values.every((v,i)=>i===0||v>values[i-1]));
 assert(values.slice(1).every(kw=>kw+1e-6>=7.8*factor&&kw<=16*factor+1e-6));
}
context.state.three_power_per_amp_kw=.70;context.state.min_charge_a=10;
assert.equal(steps()[1],7,'a commissioned expert current floor remains effective');
context.state.fixed_charge_phases=1;assert(steps().slice(1).every(kw=>kw<=3.5));
for(const file of ['main/dashboard.html','main/dashboard.js','tools/expert_ui.js','tools/expert-demo-api.js']){
 const text=fs.readFileSync(file,'utf8');
 assert(!/charge-minimum-power|expert-minimum-power|minimum_power_kw|\/api\/charge-limits/.test(text),file+' retains the removed preset selection');
}
console.log('PASS: explicit 5.6-kW first step, voltage/current bounds, commissioned expert floor, single-phase steps, removed preset selectors');
