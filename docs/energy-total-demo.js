/* Aggregate demand for the isolated demo. No device or control operations. */
(()=>{
 'use strict';
 const finite=Number.isFinite;
 const unknown=()=>({totalW:null,batteryChargeW:null,consumerW:null});
 function model(s,online){
  if(!s||!online||s.basic_mode||!(s.zero_feed_enabled||s.grid_guard_enabled)||
     !s.house_meter_ok||!finite(s.house_power_w)||!s.pv_display_enabled||
     !finite(s.pv_generation_w)||s.pv_generation_w<0||!s.battery_protect||
     s.battery_power_ok===false||!finite(s.battery_charge_w)||
     !finite(s.battery_discharge_w)||s.battery_charge_w<0||s.battery_discharge_w<0)return unknown();
  // Directional counters can contain small residual noise. Substantial charging
  // and discharging at the same time cannot describe one physical battery.
  if(s.battery_charge_w>50&&s.battery_discharge_w>50)return unknown();
  // The grid meter already includes every consumer behind the connection.
  // Submeter readings only split this total; adding them would count twice.
  // A negative grid value is export and must be deducted from local demand.
  const totalW=s.pv_generation_w+s.house_power_w+s.battery_discharge_w;
  const consumerW=totalW-s.battery_charge_w;
  if(!finite(totalW)||!finite(consumerW)||totalW<0||consumerW<0)return unknown();
  return {totalW,batteryChargeW:s.battery_charge_w,consumerW};
 }
 if(typeof module==='object'&&module.exports)module.exports={model};
 if(typeof window!=='undefined')window.TeeNetDemandDemo={model};
})();
