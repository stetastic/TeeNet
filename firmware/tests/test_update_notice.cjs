const fs=require('node:fs'),vm=require('node:vm'),assert=require('node:assert/strict');
const source=fs.readFileSync('main/dashboard.js','utf8').split('// Automatic update notice:')[1].split('showPage();writes();poll();')[0];
function setup(options={}){
 const nodes={},listeners={},requests=[];
 function el(id){return nodes[id]??={open:false,textContent:'',hidden:true,listeners:{},addEventListener(k,fn){this.listeners[k]=fn;},showModal(){this.open=true;},close(){this.open=false;},scrollIntoView(){}};}
 const ctx={$:el,online:true,initialized:true,state:{terms_accepted:true,telemetry_decided:true,version:'1.21'},busy:false,uploading:false,githubRelease:null,Date,
  document:{hidden:false,documentElement:{dataset:{demo:options.demo?'true':'false'}},querySelector:()=>options.otherDialog||el('update-available-dialog').open,addEventListener(){}},
  window:{location:{hash:''},addEventListener(k,fn){listeners[k]=fn;}},showPage(){},setTimeout(fn){fn();},
  api:async(path,body)=>{requests.push({path,body});if(options.error)throw Error('Offline');return body?{}:(options.responses?.shift()||{ok:true,newer:true,version:'1.22',running:false,checked_age_s:60});}
 };
 vm.runInNewContext('// Automatic update notice:'+source.replace(/\}\)\(\);\s*$/,'window.tick=tick;})();'),ctx);
 return {ctx,el,listeners,requests,tick:()=>ctx.window.tick()};
}
(async()=>{
 let s=setup();await s.tick();assert(s.el('update-available-dialog').open);assert.match(s.el('update-available-text').textContent,/1.22/);assert.equal(s.requests.length,1);
 s.el('update-available-later').listeners.click();await s.tick();assert(!s.el('update-available-dialog').open);assert.equal(s.requests.length,1,'Do not recheck or reopen a dismissed version on every status poll');
 s.listeners['teenet-release']({detail:{ok:true,newer:true,version:'1.23'}});assert(s.el('update-available-dialog').open,'A later version gets its own notice');
 s.el('update-available-view').listeners.click();assert.equal(s.ctx.window.location.hash,'#firmware-update');assert.equal(s.ctx.githubRelease.version,'1.23');assert(!s.el('github-install').hidden);assert.equal(s.requests.length,1,'Viewing an update never starts installation or stops charging');
 s=setup({responses:[{ok:true,newer:false,version:'1.21',checked_age_s:30}]});await s.tick();assert(!s.el('update-available-dialog').open,'Same or older firmware never prompts');
 s=setup({responses:[{ok:false,checked_age_s:-1},{ok:true,newer:true,version:'1.22',running:false}]});await s.tick();assert(s.el('update-available-dialog').open);assert.equal(s.requests.filter(x=>x.body).length,1);assert.equal(s.requests.find(x=>x.body).body.action,'check');
 for(const option of [{demo:true},{otherDialog:true}]){s=setup(option);await s.tick();assert.equal(s.requests.length,0);}
 s=setup();s.ctx.state.terms_accepted=false;await s.tick();assert.equal(s.requests.length,0);s.ctx.state.terms_accepted=true;s.ctx.document.hidden=true;await s.tick();assert.equal(s.requests.length,0);
 s=setup({error:true});await s.tick();await s.tick();assert.equal(s.requests.length,1,'Failures back off instead of flooding GitHub');
 console.log('PASS: automatic update checks, shared cache, newer-only notice, defer, dismiss, explicit installation, demo isolation and failure backoff');
})().catch(error=>{console.error(error);process.exitCode=1;});
