const fs=require('node:fs'),vm=require('node:vm'),assert=require('node:assert/strict');
const source=fs.readFileSync('tools/expert_ui.js','utf8'),schema=JSON.parse(fs.readFileSync('preview/expert-schema.json','utf8'));
const rows=schema.parameters.map(p=>({key:p.id,min:p.min,max:p.max,standard:p.default,input:{value:p.default}}));
const chart={};const ctx={rows,snapshot:{settings:{max_charge_a:16}},value:r=>Number(r.input.value),el:()=>chart};
vm.createContext(ctx);vm.runInContext(source.slice(source.indexOf(' function curveModel('),source.indexOf(' function update()')),ctx);
let m=ctx.curveModel();assert.equal(m.startWait,30);assert.equal(m.topAt,70);assert.equal(m.reduceAt,90);
assert(Math.abs(m.start-8.8)<.001);assert.equal(m.points.at(-1)[1],7.8);
ctx.drawCurve();assert(chart.innerHTML.includes('Einphasig'));assert(chart.innerHTML.includes('Dreiphasig'));
assert(chart.innerHTML.includes('3,68 kW'));assert(chart.innerHTML.includes('11,04 kW'));
assert(!/NaN|Infinity/.test(chart.innerHTML));
rows.find(p=>p.key==='PV_LEAD').input.value=.5;m=ctx.curveModel();assert.equal(m.increment,.5);assert.equal(m.topAt,105);
rows.find(p=>p.key==='PV_STEP').input.value='';m=ctx.curveModel();assert.equal(m.step,1,'invalid draft uses default, does not break chart');
ctx.snapshot.settings.max_charge_a=12;m=ctx.curveModel();assert.equal(m.maximum,12);ctx.drawCurve();assert(!/NaN|Infinity/.test(chart.innerHTML));
console.log('PASS: 1/3-phase power curves, start margin, ramp timing, measurement lead and invalid drafts');

for(const p of rows)p.input.value=p.standard;
rows.find(p=>p.key==='PV_LEAD').input.value=.1;
m=ctx.curveModel();assert.equal(m.peak,m.start,'measurement lead below deadband prevents ramp');
for(const p of rows)p.input.value=p.standard;
ctx.snapshot.settings.max_charge_a=16;
rows.find(p=>p.key==='PV_STEP').input.value=.2;
m=ctx.curveModel();assert(m.peak<16&&m.peak>15.8,'last increment below deadband is held');
for(const p of rows)p.input.value=p.standard;
Object.assign(ctx.snapshot.settings,{pv_allocation_enabled:true,battery_protect:true,zero_feed_enabled:true,pv_priority:1,pv_car_priority_w:6000});
assert.equal(ctx.curveModel(1).maximum,16);
assert(Math.abs(ctx.curveModel(3).maximum*690-6000)<4);
ctx.drawCurve();assert(chart.innerHTML.includes('Idealisiert'));assert(!chart.innerHTML.includes('Maximalleistung nach'));
console.log('PASS: deadband, per-phase priority limit and idealized labeling');

