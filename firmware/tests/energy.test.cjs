const {test}=require('node:test');
const assert=require('node:assert/strict');
const {model}=require('../main/energy.js');
const live={basic_mode:false,zero_feed_enabled:true,feedback_ok:true,actual_a:[9,0,0],wallbox_w:2070,house_meter_ok:true,house_power_w:2000,pv_display_enabled:true,pv_generation_w:1500,battery_protect:true,battery_power_ok:true,battery_soc_ok:true,battery_soc_pct:60,battery_charge_w:0,battery_discharge_w:1200,mode:'pv'};
test('mixed PV, battery and grid retain independent flow directions',()=>{
 const m=model(live,true);assert.equal(m.scene,'grid');assert.equal(m.pvFlow,true);assert.equal(m.batteryFlow,'discharge');assert.equal(m.gridFlow,'import');assert.equal(m.carHappy,false);assert.equal(m.houseLoad,2630);
 assert.equal(m.houseMood,'neutral');
});

test('predominantly solar supply makes the house and charging car happy despite grid import',()=>{
 const solar={...live,pv_generation_w:9430,house_power_w:570,battery_discharge_w:0,wallbox_w:8340,actual_a:[12.1,12.1,12.1]};
 for(const mode of ['pv','manual']){
  const m=model({...solar,mode},true);
  assert.equal(m.houseMood,'happy');assert.equal(m.carHappy,true);
  assert.equal(m.gridFlow,'import');assert.equal(m.houseLoad,1660);
 }
 const stopped=model({...solar,actual_a:[0,0,0],wallbox_w:0},true);
 assert.equal(stopped.houseMood,'happy');assert.equal(stopped.carHappy,false);
});

test('grid dominance, balanced mixes, battery supply and missing values have distinct moods',()=>{
 const known={...live,battery_discharge_w:0};
 assert.equal(model({...known,pv_generation_w:2000,house_power_w:8000},true).houseMood,'sad');
 for(const pv of [4500,5000,5500]){
  const m=model({...known,pv_generation_w:pv,house_power_w:10000-pv},true);
  assert.equal(m.houseMood,'neutral');assert.equal(m.carHappy,false);
 }
 assert.equal(model({...known,pv_generation_w:0,house_power_w:0,battery_discharge_w:4000},true).houseMood,'neutral');
 for(const extra of [{house_meter_ok:false},{pv_display_enabled:false},{battery_power_ok:false}]){
  const m=model({...known,...extra},true);assert.equal(m.houseMood,'neutral');assert.equal(m.carHappy,false);
 }
 assert.equal(model(known,false).houseMood,'neutral');
});
test('zero, missing and offline values are distinct and never animate stale data',()=>{
 assert.equal(model({...live,house_power_w:0,pv_generation_w:0},true).gridFlow,'idle');
 const missing=model({...live,house_meter_ok:false,pv_generation_w:null,battery_power_ok:false},true);assert.equal(missing.grid,null);assert.equal(missing.pvFlow,false);assert.equal(missing.batteryFlow,'unknown');
 assert.equal(missing.houseLoad,null);
 const offline=model(live,false);assert.equal(offline.charging,false);assert.equal(offline.grid,null);assert.equal(offline.solarVisible,false);assert.equal(offline.batteryVisible,false);
});
test('optional equipment stays hidden; vehicle presence is independent of SOC',()=>{
 const m=model({...live,battery_protect:false,pv_display_enabled:false,vehicle_soc_enabled:false},true);assert.equal(m.solarVisible,false);assert.equal(m.batteryVisible,false);assert.equal(m.carSoc,null);assert.equal(m.carVisible,true);
 assert.equal(model({...live,actual_a:[0,0,0],vehicle_status_known:true,vehicle_connection_state:'disconnected'},true).carVisible,false);
 assert.equal(model({...live,vehicle_status_known:true,vehicle_connection_state:'disconnected'},true).carVisible,true);
});

test('basic mode and disabled house-meter functions never retain old flow values',()=>{
 const stale={...live,vehicle_soc_enabled:true,car_soc_pct:64};
 const basic=model({...stale,basic_mode:true},true);
 assert.equal(basic.gridVisible,false);assert.equal(basic.grid,null);
 assert.equal(basic.solarVisible,false);assert.equal(basic.batteryVisible,false);assert.equal(basic.carSoc,null);
 const noGrid=model({...stale,zero_feed_enabled:false,grid_guard_enabled:false},true);
 assert.equal(noGrid.gridVisible,false);assert.equal(noGrid.gridFlow,'unknown');assert.equal(noGrid.grid,null);
 assert.equal(noGrid.solarVisible,true);assert.equal(noGrid.batteryVisible,true);
 const restored=model({...stale,zero_feed_enabled:false,grid_guard_enabled:true},true);
 assert.equal(restored.gridVisible,true);assert.equal(restored.grid,2000);
});

test('house demand balances generation, storage and the separate wallbox branch',()=>{
 for(const values of [
  {pv_generation_w:9000,house_power_w:-2500,battery_charge_w:5000,battery_discharge_w:0,wallbox_w:0},
  {pv_generation_w:4500,house_power_w:2000,battery_charge_w:0,battery_discharge_w:3000,wallbox_w:7000},
  {pv_generation_w:0,house_power_w:4500,battery_charge_w:0,battery_discharge_w:2500,wallbox_w:6000}
 ]){
  const m=model({...live,...values},true);
  assert.ok(m.houseLoad>=0);
  assert.equal(m.pv+m.grid-m.battery-m.carPower-m.houseLoad,0);
 }
});

test('inconsistent readings do not invent zero house consumption',()=>{
 const m=model({...live,pv_generation_w:1000,house_power_w:0,battery_charge_w:0,battery_discharge_w:0,wallbox_w:7000},true);
 assert.equal(m.houseLoad,null);
 assert.equal(m.houseVisible,true);
 assert.equal(m.houseNote,'Messwerte werden abgeglichen');
});

test('house position survives charging transients and missing samples',()=>{
 for(const values of [{wallbox_w:12000},{feedback_ok:false},{house_meter_ok:false}]){
  const m=model({...live,...values},true);
  assert.equal(m.houseLoad,null);assert.equal(m.houseVisible,true);
 }
 assert.equal(model(live,false).houseVisible,true);
 assert.equal(model({...live,basic_mode:true},true).houseVisible,false);
 assert.equal(model({...live,zero_feed_enabled:false,grid_guard_enabled:false},true).houseVisible,false);
});

test('moon replaces the sun only below a known 0.1 kW PV reading',()=>{
 for(const pv of [0,6.6,99.9]){
  const m=model({...live,pv_generation_w:pv},true);
  assert.equal(m.pvNight,true);assert.equal(m.solarVisible,true);
 }
 for(const pv of [100,100.1,1000])assert.equal(model({...live,pv_generation_w:pv},true).pvNight,false);
 for(const extra of [{pv_generation_w:null},{pv_display_enabled:false},{basic_mode:true}]){
  const m=model({...live,...extra},true);assert.equal(m.pvNight,false);assert.equal(m.solarVisible,false);
 }
 assert.equal(model(live,false).pvNight,false);
});

test('phase detail preserves zero readings and marks estimates, never stale currents',()=>{
 const measured=model({...live,actual_a:[10.2,0,0],power_is_extrapolated:true},true);
 assert.deepEqual(measured.phaseCurrents,[10.2,0,0]);assert(measured.currentsEstimated);
 assert.equal(model(live,true).currentsEstimated,false);
 for(const s of [{...live,feedback_ok:false},{...live,actual_a:[9,null,0]},{...live,actual_a:[9,-1,0]}]){
  assert.equal(model(s,true).phaseCurrents,null);assert.equal(model(s,true).currentsEstimated,false);
 }
 assert.equal(model(live,false).phaseCurrents,null);
});
