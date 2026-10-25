'use strict';
const assert=require('node:assert/strict');
const {model}=require('../tools/heatpump-demo-energy.js');
const base={basic_mode:false,heatpump_enabled:true,heatpump_ok:true,heatpump_w:2100,
 zero_feed_enabled:true,house_meter_ok:true,house_power_w:550,pv_display_enabled:true,pv_generation_w:10000,
 battery_protect:true,battery_power_ok:true,battery_charge_w:0,battery_discharge_w:1000,
 feedback_ok:true,actual_a:[10,10,10],wallbox_w:7000};
const m=model(base,true);
assert.equal(m.enabled,true);assert.equal(m.power,2100);assert.equal(m.houseLoad,2450);
assert.equal(base.pv_generation_w+base.house_power_w+base.battery_discharge_w,
 m.houseLoad+m.power+base.wallbox_w+base.battery_charge_w,'all displayed powers close the energy balance');
assert.equal(model({...base,house_power_w:-500},true).houseLoad,1400,'export is a negative source');
assert.equal(model({...base,heatpump_w:0},true).houseLoad,4550,'known idle differs from unknown');
assert.equal(model({...base,heatpump_w:0},true).flow,'idle');
for(const change of [{heatpump_ok:false},{heatpump_w:null},{heatpump_w:NaN},{heatpump_w:-1}]){
 const unknown=model({...base,...change},true);assert.equal(unknown.power,null);assert.equal(unknown.houseLoad,null);assert.equal(unknown.flow,'unknown');
}
assert.equal(model(base,false).power,null,'offline values are not live');
assert.equal(model(base,false).houseLoad,null);
assert.equal(model({...base,basic_mode:true},true).enabled,false);
assert.equal(model({...base,heatpump_enabled:false},true).enabled,false);
assert.equal(model({...base,battery_power_ok:false},true).houseLoad,null);
assert.equal(model({...base,pv_display_enabled:false},true).houseLoad,null);
assert.equal(model({...base,house_meter_ok:false},true).houseLoad,null);
assert.equal(model({...base,heatpump_w:20000},true).houseLoad,null,'inconsistent measurements never become a negative house load');
assert.equal(model({...base,actual_a:[0,0,0],wallbox_w:0},true).houseLoad,9450,'heat pump remains a consumer while the wallbox is stopped');
console.log('Heat-pump demo energy: balance, disabled, stopped, zero, missing, stale/offline and inconsistent data passed.');
