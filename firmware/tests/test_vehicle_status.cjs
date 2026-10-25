const fs=require('node:fs'),vm=require('node:vm'),assert=require('node:assert/strict');
const source=fs.readFileSync('main/dashboard.js','utf8');
const start=source.indexOf('function vehicleStatusView('),end=source.indexOf('\nfunction render(){',start);
assert(start>0&&end>start);
const ctx=vm.createContext({});vm.runInContext(source.slice(start,end),ctx);
const view=value=>ctx.vehicleStatusView(value);

assert.deepEqual({...view({vehicle_status_enabled:false})},{visible:false,state:'unknown',text:'Status nicht verfügbar'});
for(const [state,text] of Object.entries({
  disconnected:'Nicht angeschlossen',connected:'Angeschlossen',charging:'Lädt',
  finished:'Ladung beendet',fault:'Störung'
}))assert.deepEqual({...view({vehicle_status_enabled:true,vehicle_status_known:true,vehicle_connection_state:state})},
  {visible:true,state,text});
assert.equal(view({vehicle_status_enabled:true,vehicle_status_known:false,vehicle_connection_state:'charging'}).state,'unknown');
assert.equal(view({vehicle_status_enabled:true,vehicle_status_known:true,vehicle_connection_state:'unexpected'}).text,'Status nicht verfügbar');
const html=fs.readFileSync('main/dashboard.html','utf8');
assert(html.includes('id="vehicle-state-row"')&&html.includes('id="vehicle-state"'));
console.log('PASS: vehicle status is concise, observational and hidden without Shell network data');
