const fs=require('node:fs'),vm=require('node:vm'),assert=require('node:assert/strict');
const source=fs.readFileSync('main/dashboard.js','utf8');
const html=fs.readFileSync('main/dashboard.html','utf8'),css=fs.readFileSync('main/dashboard.css','utf8');
assert(!html.includes('name="homeassistant_enabled"'),'Home Assistant discovery has no misleading switch');
assert(html.includes('ioBroker oder Home Assistant')&&html.includes('id="theme-dark"'));
assert(source.includes("localStorage.setItem('teennet-theme'")&&css.includes('html[data-theme="dark"]'));
function part(a,b){const start=source.indexOf(a),end=source.indexOf(b,start);assert(start>=0&&end>start);return source.slice(start,end);}
const nodes={};for(const id of ['session-total','session-grid','session-solar','session-cost','session-rows','session-empty','session-pages'])nodes[id]={value:'',textContent:'old result',replaceChildren(){this.cleared=true;}};
nodes['session-range']={value:'1'};
const requests=[];
const ctx=vm.createContext({$,console,Date,Number,Map,state:{current_date:20260301},sessionsBusy:false,sessionPageOffset:0,online:true,document:{hidden:false},lastSessionsAt:0,api(path){requests.push(path);throw Error('Offline');}});
function $(id){return nodes[id];}
vm.runInContext(part('function shellPinChoices(','function updateShellPins(')+part('function clearSessions(','function shellPinChoices('),ctx);
const run=code=>vm.runInContext(code,ctx);
ctx.values={house_meter_type:'tasmota',wallbox_tx_pin:17,wallbox_rx_pin:18};
const choices=()=>Array.from(run("shellPinChoices(values,'wallbox_tx_pin')"));
assert.equal(choices().find(x=>x.pin===17).reason,'');
assert.equal(choices().find(x=>x.pin===18).reason,'Shell RX');
assert(choices().find(x=>x.pin===4).reason);
assert(!choices().some(x=>[0,3,19,20,26,35,36,37,45,46,48].includes(x.pin)));
ctx.values.relay_board_enabled=true;assert(choices().find(x=>x.pin===12).reason);
ctx.values.evu_input_enabled=true;assert(choices().find(x=>x.pin===7).reason);
ctx.values.external_mode_input_enabled=true;assert(choices().find(x=>x.pin===6).reason);
ctx.values.phase_switch_enabled=true;assert.equal(choices().find(x=>x.pin===13).reason,'','retired feedback contacts reserve no pin');
ctx.values.house_meter_type='sdm630';assert(choices().find(x=>x.pin===43).reason);
ctx.values.house_meter_type='huawei';assert.equal(choices().find(x=>x.pin===43).reason,'');
(async()=>{
  await run('loadSessions()');
  assert.deepEqual(requests,['/api/sessions?from=2026-03-01&to=2026-03-01&offset=0']);
  assert.equal(run('sessionDateQuery(3)'),'from=2026-02-27&to=2026-03-01');
  assert.equal(run('sessionDateQuery(30)'),'from=2026-01-31&to=2026-03-01');
  for(const id of ['session-total','session-grid','session-solar','session-cost'])assert.equal(nodes[id].textContent,'—');
  assert.equal(nodes['session-rows'].cleared,true);assert.equal(nodes['session-empty'].hidden,false);
  assert(!html.includes('id="export-range"'));
  assert(html.includes('href="/api/export.json"') && html.includes('href="/api/export.csv"'));
  console.log('PASS: pin reservations, weekly history and export controls, unavailable sessions clear totals');
})().catch(error=>{console.error(error);process.exitCode=1;});
