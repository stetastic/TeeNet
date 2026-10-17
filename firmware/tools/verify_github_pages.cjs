const fs=require('node:fs'),path=require('node:path'),vm=require('node:vm'),assert=require('node:assert/strict');
const root=path.resolve('../github-release-1.11/docs');
const location={origin:'https://stetastic.github.io',pathname:'/TeeNet/',search:''};
const store=new Map(),requests=[];
const window={location,fetch:async(input)=>{
 const url=new URL(typeof input==='string'?input:input.url,location.origin);requests.push(url.href);
 assert.equal(url.origin,location.origin,'Demo must never contact the real telemetry endpoint');
 assert(url.pathname.startsWith('/TeeNet/'),'Static asset must retain the Pages project prefix');
 const file=path.join(root,url.pathname.slice('/TeeNet/'.length));assert(fs.existsSync(file),url.href);
 return new Response(fs.readFileSync(file),{headers:{'Content-Type':file.endsWith('.json')?'application/json':'text/plain'}});
}};
const ctx={window,location,URL,URLSearchParams,Response,Date,Math,Number,String,Map,JSON,document:{addEventListener(){}},localStorage:{getItem:key=>store.get(key)||null,setItem:(key,v)=>store.set(key,v)}};
for(const name of ['demo-api.js','evcc-demo-api.js','expert-demo-api.js'])vm.runInNewContext(fs.readFileSync(path.join(root,name),'utf8'),ctx);
(async()=>{
 const read=async()=>(await window.fetch('/api/status')).json();
 const post=async(url,data)=>window.fetch(url,{method:'POST',body:JSON.stringify(data)});
 let status=await read();assert.equal(status.version,'1.21');assert.equal(status.telemetry_enabled,false);
 assert.equal((await post('/api/telemetry',{enabled:true,revision:'2026-10-16.1'})).status,400);
 assert.equal((await post('/api/terms',{accepted:true,revision:status.terms_revision})).status,200);
 assert.equal((await post('/api/telemetry',{enabled:true,revision:'2026-10-16.1'})).status,200);
 status=await read();assert(status.telemetry_enabled&&status.telemetry_decided);assert.equal(status.telemetry_last_ok,false);
 assert.equal((await post('/api/telemetry',{enabled:false,revision:'2026-10-16.1'})).status,200);
 assert.equal((await read()).telemetry_enabled,false);
 assert(requests.every(url=>!url.includes('workers.dev')));
 for(const name of ['guide','help','downloads'])assert(fs.existsSync(path.join(root,name,'index.html')));
 for(const name of fs.readdirSync(root).filter(n=>n.endsWith('.js')))require('node:child_process').execFileSync(process.execPath,['--check',path.join(root,name)]);
 console.log('PASS: GitHub Pages base paths, synthetic APIs, consent, opt-out, no real heartbeats, help/download routes and JavaScript syntax');
})().catch(e=>{console.error(e);process.exitCode=1;});
