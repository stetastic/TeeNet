const fs=require('node:fs'),vm=require('node:vm'),assert=require('node:assert/strict');
const location={origin:'http://demo.local',search:''};
const window={location,fetch(){throw Error('No real network in demo');}};
vm.runInNewContext(fs.readFileSync('preview/demo-api.js','utf8'),{window,location,URL,URLSearchParams,Response,Date,Math,Number,String,Map,JSON,document:{addEventListener(){}}});
const read=async(query)=>(await window.fetch('/api/sessions?'+query)).json();
const date=value=>`${String(value).slice(0,4)}-${String(value).slice(4,6)}-${String(value).slice(6,8)}`;
(async()=>{
  const all=await read('');assert.equal(all.count,5);assert.equal(all.retained,9);
  const post=async(path,data)=>(await window.fetch(path,{method:'POST',body:JSON.stringify(data)})).json();
  assert.equal((await post('/api/config',{wallbox_tx_pin:8,wallbox_rx_pin:9})).reboot_required,true);
  assert.equal((await (await window.fetch('/api/config')).json()).wallbox_tx_pin,8);
  assert.equal((await (await window.fetch('/api/status')).json()).reboot_required,true);
  await post('/api/reboot',{});
  assert.equal((await (await window.fetch('/api/status')).json()).reboot_required,false);
  const last=all.days.at(-1),day=date(last.date);
  const one=await read(`from=${day}&to=${day}`);
  assert.equal(one.count,1);assert.equal(one.days[0].date,last.date);assert(Math.abs(one.cost_eur-last.cost_eur)<.00001);
  const csv=await (await window.fetch(`/api/export.csv?from=${day}&to=${day}`)).text();
  assert.equal(csv.trim().split('\n').length,last.sessions+2);assert(csv.includes(';Netz_Kosten_EUR;'));assert(csv.includes(';Solar_Akku_Kosten_EUR;'));
  assert.equal((await read('period=2024-01')).count,0);
  assert.equal((await read('from=2026-10-06&to=2026-10-05')).count,0);
  for(const month of new Set(all.days.map(s=>date(s.date).slice(0,7)))){
    const got=await read('period='+month),expected=all.days.filter(s=>date(s.date).startsWith(month));
    assert.equal(got.count,expected.length);assert(Math.abs(got.cost_eur-expected.reduce((n,s)=>n+s.cost_eur,0))<.00001);
  }
  console.log('PASS: demo month/custom filters, day totals and individual CSV costs match');
})().catch(error=>{console.error(error);process.exitCode=1;});
