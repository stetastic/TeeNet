const fs=require('node:fs'),vm=require('node:vm'),assert=require('node:assert/strict');
const source=fs.readFileSync('main/dashboard.js','utf8');
const start=source.indexOf('async function waitGitHub(){'),end=source.indexOf("$('ota-file').addEventListener('change'",start);
assert(start>0&&end>start,'test the current controller-backed update workflow');
function fixture(result){
 const nodes=new Map(),requests=[],tasks=[],events=[];
 const node=id=>{if(!nodes.has(id))nodes.set(id,{hidden:false,textContent:'',value:'old-file',clicks:0,listeners:{},click(){this.clicks++;},addEventListener(type,callback){this.listeners[type]=callback;}});return nodes.get(id);};
 const context={
  $:node,Date,busy:false,uploading:false,online:true,state:{version:'1.21'},
  window:{dispatchEvent(event){events.push(event);}},CustomEvent:function(type,options){this.type=type;this.detail=options.detail;},
  action(work){const task=work();tasks.push(task);return task;},
  api:async(path,body)=>{requests.push({path,body});assert.equal(path,'/api/update/github');return body?{}:result;},
  setTimeout(){throw Error('No polling delay expected for completed synthetic checks');}
 };
 vm.createContext(context);vm.runInContext(source.slice(start,end),context);
 return {context,node,requests,tasks,events,check:()=>node('github-check').listeners.click(),choose:()=>node('ota-upload').listeners.click()};
}
(async()=>{
 let f=fixture({ok:true,running:false,newer:true,version:'1.22'});
 assert.equal(f.requests.length,0,'rendering the update controls does not start a check');
 await f.check();assert.equal(f.requests.length,2);assert.equal(f.requests[0].body.action,'check');assert.equal(f.requests[1].body,undefined);
 assert.equal(f.node('github-install').hidden,false);assert.equal(f.node('github-install').textContent,'TeeNet 1.22 installieren');
 assert.equal(f.node('github-status').textContent,'TeeNet 1.22 verfügbar.');assert.equal(f.events[0].type,'teenet-release');
 assert.equal(f.requests.some(request=>request.body?.action==='install'),false,'checking never installs an update');
 const requests=f.requests.length;f.choose();assert.equal(f.node('ota-file').value,'');assert.equal(f.node('ota-file').clicks,1);
 assert.equal(f.requests.length,requests,'choosing a local file performs no device request');assert.equal(f.context.uploading,false);
 for(const flag of ['busy','uploading']){f.context[flag]=true;f.choose();assert.equal(f.node('ota-file').clicks,1);f.context[flag]=false;}
 f.context.online=false;f.choose();assert.equal(f.node('ota-file').clicks,1);
 f=fixture({ok:true,running:false,newer:false,version:'1.20'});await f.check();
 assert.equal(f.node('github-install').hidden,true);assert.match(f.node('github-status').textContent,/Installierte Version 1.21 ist aktuell oder neuer/);
 f=fixture({ok:false,running:false,message:'Synthetic release unavailable'});await assert.rejects(f.check(),/Synthetic release unavailable/);
 assert.equal(f.node('github-install').hidden,true,'failed checks cannot expose an install action');
 assert(!fs.readFileSync('main/dashboard.html','utf8').includes('donate-versions'));
 console.log('PASS: controller-backed release checks, explicit installation, current local file chooser and failed-check isolation');
})().catch(error=>{console.error(error);process.exitCode=1;});
