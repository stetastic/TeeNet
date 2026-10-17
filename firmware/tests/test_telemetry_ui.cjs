const fs=require('node:fs'),vm=require('node:vm'),assert=require('node:assert/strict');
function setup(accepted=false){
 const nodes={},requests=[];
 function el(id){return nodes[id]??={checked:false,disabled:false,open:false,textContent:'',listeners:{},addEventListener(event,fn){this.listeners[event]=fn;},showModal(){this.open=true;},close(){this.open=false;}};}
 const state={terms_accepted:accepted,terms_revision:'2026-10-09.2',telemetry_revision:'2026-10-16.1',telemetry_decided:false,telemetry_enabled:false};
 const ctx={state,online:true,document:{getElementById:el,querySelectorAll:()=>[],documentElement:{dataset:{demo:'true'}}},window:{addEventListener(){}},writes(){},pollStatus:async()=>{},api:async(path,body)=>{requests.push({path,body});if(path==='/api/terms')state.terms_accepted=true;else if(path==='/api/telemetry'){state.telemetry_decided=true;state.telemetry_enabled=body.enabled;}else throw Error('Unexpected external request');}};
 vm.runInNewContext(fs.readFileSync('main/terms.js','utf8'),ctx);ctx.window.teenetTerms.updateWrites();
 return {el,state,requests,ctx};
}
(async()=>{
 for(const wanted of [true,false]){
  const s=setup();assert(s.el('terms-telemetry').checked,'Participation is preselected as requested');assert(s.el('terms-dialog').open);assert.equal(s.requests.length,0,'Opening setup or polling never constitutes consent');
  s.ctx.window.teenetTerms.updateWrites();assert.equal(s.requests.length,0);
  s.el('terms-confirm').checked=true;s.el('terms-telemetry').checked=wanted;await s.el('terms-accept').listeners.click();
  assert.equal(s.requests.length,2);assert.equal(s.requests[1].path,'/api/telemetry');assert.equal(s.requests[1].body.enabled,wanted);assert(!s.el('terms-dialog').open);
 }
 const upgraded=setup(true);upgraded.el('terms-telemetry').checked=false;await upgraded.el('terms-accept').listeners.click();
 assert.equal(upgraded.requests.length,1,'Choosing statistics after an update must not re-submit terms and stop charging');
 const s=setup(true);s.state.telemetry_decided=true;s.ctx.window.teenetTerms.updateWrites();s.el('telemetry-enabled').checked=true;
 await s.el('telemetry-enabled').listeners.change({stopPropagation(){}});assert(s.state.telemetry_enabled);
 s.el('telemetry-enabled').checked=false;await s.el('telemetry-enabled').listeners.change({stopPropagation(){}});assert(!s.state.telemetry_enabled);
 assert(s.el('telemetry-status').textContent.includes('Demo sendet keine Lebenszeichen'));
 console.log('PASS: preselected but explicit consent; decline, existing installations, opt-out and isolated demo behavior');
})().catch(e=>{console.error(e);process.exitCode=1;});
