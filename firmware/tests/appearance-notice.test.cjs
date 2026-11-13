const {test}=require('node:test'),assert=require('node:assert/strict'),fs=require('node:fs'),vm=require('node:vm');
const source=fs.readFileSync('main/energy.js','utf8');
function setup(server={seen:false},missing=new Set()){
 const nodes={},events={},requests=[],storage=new Map();let otherDialog=false,error=false;
 const node=id=>nodes[id]??={dataset:{},open:false,value:'manual',textContent:'',className:'',hidden:false,disabled:false,attrs:{},listeners:{},querySelector(selector){return selector==='.en-wallbox-node'?null:node(id+' '+selector);},setAttribute(key,value){this.attrs[key]=value;},addEventListener(event,fn){this.listeners[event]=fn;},showModal(){this.open=true;},close(){this.open=false;}};
 const context={Number,module:{exports:{}},window:{addEventListener(event,fn){events[event]=fn;}},
  document:{documentElement:{dataset:{}},hidden:false,getElementById:id=>missing.has(id)?null:node(id),addEventListener(){},querySelector(selector){return selector==='dialog[open]'?(otherDialog||node('graphic-notice').open):node(selector);}},
  localStorage:{getItem:key=>storage.get(key),setItem:(key,value)=>storage.set(key,value)},
  api:async(path,body)=>{requests.push({path,body});if(error)throw Error('Offline');if(body.graphic_notice_ack===true)server.seen=true;return{};}};
 vm.runInNewContext(source,context);
 return {api:context.window.TeeNetEnergy,node,events,requests,dataset:context.document.documentElement.dataset,storage,setOther(value){otherDialog=value;},setError(value){error=value;}};
}
test('retired presentation popup never opens even when old firmware reports it pending',()=>{
 const s=setup();s.api.loadNotice(true);s.api.update({terms_accepted:true,telemetry_decided:true},true);
 s.events['teenet-status']?.();assert(!s.node('graphic-notice').open);assert.equal(s.requests.length,0);
 assert(!fs.readFileSync('main/dashboard.html','utf8').includes('id="graphic-notice"'));
});

test('layout defaults to familiar standard and persists independently of the view',async()=>{
 const s=setup();assert.equal(s.dataset.dashboardDensity,'standard');assert(s.node('battery-options').open);
 s.api.setView('graphic');
 await s.node('density-compact').listeners.click();
 assert.equal(s.dataset.dashboardDensity,'compact');assert.equal(s.dataset.dashboardView,'graphic');
 assert.equal(s.storage.get('teennet-density'),'compact');assert.equal(s.node('density-compact').attrs['aria-pressed'],'true');
 assert.equal(JSON.stringify(s.requests[0]),JSON.stringify({path:'/api/theme',body:{density:'compact'}}));assert(s.node('battery-options').open,'compact battery controls stay expanded');
 s.api.loadDensity('standard');assert.equal(s.dataset.dashboardDensity,'compact');
 await s.node('density-standard').listeners.click();assert.equal(s.dataset.dashboardDensity,'standard');assert.equal(s.dataset.dashboardView,'graphic');assert(s.node('battery-options').open);
 const reload=setup();reload.api.loadDensity('compact');assert.equal(reload.dataset.dashboardDensity,'compact');
});
test('failed layout save keeps browser choice and reports retryable failure',async()=>{
 const s=setup();s.setError(true);await s.node('density-compact').listeners.click();
 assert.equal(s.dataset.dashboardDensity,'compact');assert(!s.node('density-save-status').hidden);assert(!s.node('density-compact').disabled);
 s.setError(false);await s.node('density-compact').listeners.click();assert(s.node('density-save-status').hidden);
});

test('optional vehicle-status row may be absent in the public demo',()=>{
 const s=setup({seen:false},new Set(['vehicle-state-row','vehicle-state']));
 assert.doesNotThrow(()=>s.api.update({terms_accepted:true},true));
 assert(s.node('compact-vehicle-note').hidden);assert.equal(s.node('compact-vehicle-note').textContent,'');
});
