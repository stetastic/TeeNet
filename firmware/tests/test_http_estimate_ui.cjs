'use strict';
const assert=require('node:assert/strict');
const fs=require('node:fs');
const vm=require('node:vm');
const {model,wallboxDisplayReading:reading}=require('../main/energy.js');
const base={shell_http_control_enabled:true,enabled:true,mode:'manual',charge_phases:3,
 wallbox_estimate_valid:true,wallbox_estimate_source:'http_pilot',wallbox_estimate_age_ms:100,
 wallbox_estimated_a:[9,9,9],wallbox_estimated_w:6300,
 feedback_ok:true,actual_a:[8,8,8],wallbox_w:5600};
const frozen=JSON.stringify(base);
assert.equal(reading(base).power_w,6300);
assert.deepEqual(reading(base).phase_a,[9,9,9]);
assert(reading(base).note.includes('Geschätzt'));
assert.equal(reading({...base,shell_http_control_enabled:false}).power_w,5600);
assert.equal(reading({...base,shell_http_control_enabled:false,http_control_trial_armed:true}).power_w,6300);
assert.equal(reading({...base,shell_http_control_enabled:false,http_control_regulation_source:'shell_http_pilot'}).power_w,6300);
for(const change of [{wallbox_estimate_valid:false},{wallbox_estimate_age_ms:4000},{wallbox_estimate_age_ms:-1},
 {wallbox_estimate_age_ms:undefined},{wallbox_estimate_source:'other'},{wallbox_estimated_w:NaN},
 {wallbox_estimated_a:[9,9]},{wallbox_estimated_a:[9,NaN,9]},{wallbox_estimated_a:[-1,9,9]}]){
 const value=reading({...base,...change});assert.equal(value.power_w,null);assert.equal(value.phase_a,null);
 assert.equal(value.valid,false,'invalid predictions must not fall back to measured values under the estimate label');
}
assert.equal(reading(base,false).power_w,null,'offline must not claim current power');
for(const change of [{enabled:false},{stop_requested:true},{charge_stop_latched:true},{http_control_trial_fault:true},
 {runtime_watchdog_failed:true},{firmware_check_failed:true},{phase_switching:true},{phase_state:'fehler'},{mode:'off'}]){
 const value=reading({...base,...change,wallbox_estimate_valid:false,wallbox_estimate_age_ms:9000});
 assert.equal(value.power_w,0);assert.deepEqual(value.phase_a,[0,0,0]);assert(value.note.includes('Stoppvorgabe'));
 assert.equal(reading({...base,...change},false).power_w,null);
}
assert.equal(JSON.stringify(base),frozen,'display estimates must not overwrite physical measurements');
const energy={...base,basic_mode:false,zero_feed_enabled:true,house_meter_ok:true,house_power_w:1000,
 pv_display_enabled:true,pv_generation_w:6000,battery_protect:true,battery_power_ok:true,battery_charge_w:0,battery_discharge_w:0};
assert.equal(model(energy,true).carPower,6300);
assert.equal(model(energy,true).houseLoad,1400,'house balance retains measured 5600 W, not the 6300-W prediction');
assert.equal(model({...energy,feedback_ok:false},true).houseLoad,null,'no invented measured home load without reference');
assert.equal(model({...energy,enabled:false},true).carPower,0);
assert.equal(model({...energy,enabled:false},true).houseLoad,1400,'stopping the estimate does not prove physical zero');

const source=fs.readFileSync('main/dashboard.js','utf8');
const chargingStart=source.indexOf('function httpReportedCharging('),chargingEnd=source.indexOf('\nfunction httpControlSettingView(',chargingStart);
const statusContext=vm.createContext({});vm.runInContext(source.slice(chargingStart,chargingEnd),statusContext);
const charging=statusContext.httpReportedCharging;
const httpStatus={http_control_trial_armed:true,vehicle_status_known:true,vehicle_connection_state:'charging'};
assert.equal(charging(httpStatus,true),true);
for(const change of [{vehicle_status_known:false},{vehicle_connection_state:'connected'},{http_control_trial_armed:false}])assert.equal(charging({...httpStatus,...change},true),false);
assert.equal(charging(httpStatus,false),false);
assert.equal(charging({...base,vehicle_status_known:false},true),false,'a predicted current is no proof that the vehicle is charging');
const start=source.indexOf('function httpControlSettingView('),end=source.indexOf('\nfunction updateHttpControlSetting(',start);
assert(start>0&&end>start);
const context=vm.createContext({});vm.runInContext(source.slice(start,end),context);
const view=context.httpControlSettingView;
assert.equal(view({}, {}, true).canEnable,false);
assert.equal(view({}, {shell_http_control_enabled:false}, true).canEnable,false);
assert.equal(view({shell_http_control_available:true},{shell_http_control_enabled:false},false).canEnable,false);
assert.equal(view({shell_http_control_available:true},{shell_http_control_enabled:false},true).canEnable,true);
assert.equal(view({shell_http_control_available:false},{shell_http_control_enabled:true},true).canDisable,true);
assert.equal(view({shell_http_control_available:false,shell_http_control_block:'Prüfung offen'},{shell_http_control_enabled:false},true).note,'Prüfung offen');
const html=fs.readFileSync('main/dashboard.html','utf8');
assert(html.includes('name="shell_http_control_enabled" type="checkbox" aria-describedby'));
assert(!html.includes('name="shell_http_control_enabled" type="checkbox" disabled'));
assert(html.includes('Mercedes GLB 250')&&html.includes('Netzwerklatenzen sind noch nicht erprobt'));
console.log('PASS: labelled leading display, stale unknowns, immediate stop estimate, untouched physical measurements and capability-gated setting');
