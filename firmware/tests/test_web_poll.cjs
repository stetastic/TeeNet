const fs=require('node:fs'),vm=require('node:vm'),assert=require('node:assert/strict');
const source=fs.readFileSync('main/dashboard.js','utf8');
const start=source.indexOf("let statusPromise=null"),end=source.indexOf("window.addEventListener('resize'",start);
assert(start>0&&end>start);
let calls=0,visibility,scheduled=[],reloads=0,release,clock=0,statisticsHidden=true,settingsHidden=true;
const document={hidden:false,addEventListener(name,callback){assert.equal(name,'visibilitychange');visibility=callback;}};
const ctx=vm.createContext({document,window:{location:{reload(){reloads++;}}},state:null,token:'',online:true,
  initialized:true,busy:false,uploading:false,historyBusy:false,history:{},statusFailures:0,Math,Date:{now:()=>clock},
  $(id){return {hidden:id==='statistics'?statisticsHidden:id==='settings'?settingsHidden:true};},updateUnsavedFunctions(){},render(){},showConnection(){},loadConfig(){},writes(){},loadHistory(){},loadSessions(){},drawCharts(){},
  setTimeout(callback,delay){scheduled.push({callback,delay});return scheduled.length;},clearTimeout(){},
  async api(){calls++;if(release)await new Promise(resolve=>release.resolve=resolve);return {build_id:'test-build',token:'token'};}
});
vm.runInContext(source.slice(start,end),ctx);
async function run(code){return vm.runInContext(code,ctx);}
(async()=>{
  document.hidden=true;await run('poll()');assert.equal(calls,0);assert.equal(scheduled.length,0);
  document.hidden=false;visibility();assert.equal(scheduled.at(-1).delay,0);
  await run('poll()');assert.equal(calls,1);assert.equal(scheduled.at(-1).delay,2000);
  document.hidden=true;await run('poll()');assert.equal(calls,1);
  document.hidden=false;release={};const one=run('pollStatus()'),two=run('pollStatus()');
  await Promise.resolve();assert.equal(calls,2,'concurrent status reads must share one request');release.resolve();await Promise.all([one,two]);release=null;
  ctx.api=async()=>{throw Error('offline');};await run('poll()');assert.equal(scheduled.at(-1).delay,4000);
  await run('poll()');assert.equal(scheduled.at(-1).delay,8000);
  for(let i=0;i<8;i++)await run('poll()');assert.equal(scheduled.at(-1).delay,30000);
  ctx.api=async()=>({build_id:'new-build',token:'token'});await run('pollStatus()');assert.equal(reloads,1);
  let histories=0,sessions=0;statisticsHidden=false;
  ctx.api=async(path)=>{if(path==='/api/history')histories++;return {build_id:'test-build',token:'token',points:[]};};
  ctx.loadSessions=()=>{sessions++;run('lastSessionsAt=Date.now()');};
  await run("loadedBuild='';statusFailures=0;poll()");assert.equal(histories,1);assert.equal(sessions,1);
  clock=29000;await run('poll()');assert.equal(histories,1);assert.equal(sessions,1);
  clock=30000;await run('poll()');assert.equal(histories,1);assert.equal(sessions,2);
  clock=59000;await run('poll()');assert.equal(histories,1);assert.equal(sessions,2);
  clock=60000;await run('poll()');assert.equal(histories,2);assert.equal(sessions,3);
  statisticsHidden=true;clock=120000;await run('poll()');assert.equal(histories,2);assert.equal(sessions,3);
  ctx.state={enabled:false,feedback_ok:true,actual_a:[0,0,0]};assert.equal(await run('pollDelay()'),10000);
  settingsHidden=false;assert.equal(await run('pollDelay()'),5000);settingsHidden=true;
  ctx.state.enabled=true;assert.equal(await run('pollDelay()'),2000);
  ctx.state.enabled=false;ctx.state.feedback_ok=false;assert.equal(await run('pollDelay()'),2000);
  console.log('PASS: bounded polling, hidden tabs, request deduplication, build reload; history 60 s and sessions 30 s only while visible');
})().catch(error=>{console.error(error);process.exitCode=1;});
