'use strict';
const assert=require('node:assert/strict');
const {model}=require('../tools/energy-total-demo.js');
const base={basic_mode:false,zero_feed_enabled:true,house_meter_ok:true,house_power_w:1990,
 pv_display_enabled:true,pv_generation_w:6540,battery_protect:true,battery_power_ok:true,
 battery_charge_w:0,battery_discharge_w:540,feedback_ok:true,actual_a:[9,9,9],wallbox_w:5840,
 heatpump_enabled:true,heatpump_ok:true,heatpump_w:1990};
const expectedUnknown={totalW:null,batteryChargeW:null,consumerW:null};
assert.deepEqual(model(base,true),{totalW:9070,batteryChargeW:0,consumerW:9070},'the screenshot totals 9.07 kW');
assert.equal(model(base,true).totalW,1240+base.heatpump_w+base.wallbox_w,'the consumer split closes the same aggregate');
const charging={...base,pv_generation_w:12000,house_power_w:-1000,battery_charge_w:2000,battery_discharge_w:0};
assert.deepEqual(model(charging,true),{totalW:11000,batteryChargeW:2000,consumerW:9000},'charging the house battery belongs to the total demand');
assert.deepEqual(model({...charging,house_power_w:0},true),{totalW:12000,batteryChargeW:2000,consumerW:10000},'balanced grid exchange is known zero');
assert.deepEqual(model({...base,pv_generation_w:0,house_power_w:0,battery_charge_w:0,battery_discharge_w:0},true),
 {totalW:0,batteryChargeW:0,consumerW:0},'known idle is zero rather than unknown');
assert.equal(model({...base,pv_generation_w:7000,house_power_w:-2000,battery_discharge_w:0},true).totalW,5000,'export is deducted');
assert.equal(model({...base,pv_generation_w:0,house_power_w:6000,battery_discharge_w:0},true).totalW,6000,'night-time grid consumption is valid');
assert.equal(model({...base,pv_generation_w:0,house_power_w:-100,battery_discharge_w:1000},true).totalW,900,'battery energy exported to the grid is not local demand');
for(const change of [
 {heatpump_enabled:false},{heatpump_ok:false},{heatpump_w:null},{heatpump_w:NaN},
 {heatpump_w:100000},{feedback_ok:false},{actual_a:[]},{wallbox_w:null},{wallbox_w:NaN},
 {heatpump_ok:false,heatpump_w:null,feedback_ok:false,actual_a:[],wallbox_w:null}
])assert.equal(model({...base,...change},true).totalW,9070,'aggregate demand is independent of consumer submeters');
for(const change of [
 {basic_mode:true},{zero_feed_enabled:false,grid_guard_enabled:false},{house_meter_ok:false},
 {house_power_w:null},{house_power_w:NaN},{house_power_w:Infinity},{pv_display_enabled:false},
 {pv_generation_w:null},{pv_generation_w:NaN},{pv_generation_w:-1},{battery_protect:false},
 {battery_power_ok:false},{battery_charge_w:null},{battery_charge_w:NaN},{battery_charge_w:-1},
 {battery_discharge_w:null},{battery_discharge_w:Infinity},{battery_discharge_w:-1},
 {battery_charge_w:100,battery_discharge_w:100},{pv_generation_w:0,house_power_w:-1000,battery_discharge_w:0},
 {pv_generation_w:100,house_power_w:0,battery_charge_w:1000,battery_discharge_w:0},
 {pv_generation_w:Number.MAX_VALUE,house_power_w:Number.MAX_VALUE,battery_discharge_w:0}
])assert.deepEqual(model({...base,...change},true),expectedUnknown,'invalid, incomplete and physically inconsistent aggregate remains unknown');
assert.deepEqual(model(base,false),expectedUnknown,'offline snapshots are not live demand');
assert.deepEqual(model(null,true),expectedUnknown);
assert.equal(model({...base,zero_feed_enabled:false,grid_guard_enabled:true},true).totalW,9070,'either valid grid-meter feature permits the aggregate');
assert.equal(model({...base,battery_charge_w:20},true).consumerW,9050,'small directional residuals do not hide a valid total');
assert.equal(model({...base,battery_power_ok:undefined},true).totalW,9070,'missing optional quality flag follows the existing energy model');
console.log('Demo total demand: source balance, export, battery charging, idle, missing submeters, offline and inconsistent data passed.');
