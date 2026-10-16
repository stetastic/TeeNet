const fs=require('node:fs'),vm=require('node:vm'),assert=require('node:assert/strict');
function demo(){
  const location={origin:'http://demo.local',search:''};
  const window={location,fetch(){throw Error('Unexpected real network');}};
  const context={window,location,URL,URLSearchParams,Response,Date,Math,Number,String,Map,JSON,document:{addEventListener(){}},localStorage:{getItem:key=>key==='teenet-demo-terms'?'2026-10-09.2':null,setItem(){}}};
  vm.runInNewContext(fs.readFileSync('preview/demo-api.js','utf8'),context);
  vm.runInNewContext(fs.readFileSync('tools/evcc-demo-api.js','utf8'),context);
  return {read:async path=>(await window.fetch(path)).json(),post:async(path,data)=>(await window.fetch(path,{method:'POST',body:JSON.stringify(data)})).json()};
}
async function configuredDemo(){
  const d=demo();await d.post('/api/config',{basic_mode:false,zero_feed_enabled:true,battery_protect:true,vehicle_soc_enabled:true,pv_display_enabled:true});
  await d.post('/api/control',{mode:'pv',enabled:true});return d;
}
(async()=>{
  const fresh=demo(),freshConfig=await fresh.read('/api/config');assert.equal(freshConfig.basic_mode,true);
  for(const key of ['evcc_feature_enabled','pv_allocation_enabled','zero_feed_enabled','battery_protect','vehicle_soc_enabled','pv_display_enabled','charge_plan_enabled','external_mode_input_enabled','evu_input_enabled','grid_guard_enabled','phase_switch_enabled','relay_board_enabled','huawei_enabled','huawei_battery','huawei_pv'])assert(!freshConfig[key],key+' starts disabled');
  assert.equal((await fresh.read('/api/status')).enabled,false);
  assert.equal((await fresh.post('/api/config',{basic_mode:false})).reboot_required,false);
  assert.equal((await fresh.read('/api/config')).vehicle_soc_enabled,false,'opening extra functions must not select them');
  assert.equal((await fresh.read('/api/config')).pv_display_enabled,false,'opening extra functions must not enable the PV display');
  await fresh.post('/api/reboot',{});
  for(const key of ['vehicle_soc_enabled','pv_display_enabled'])assert.equal((await fresh.read('/api/config'))[key],false,key+' stays off after first setup and reboot');
  for(const change of [{wifi_ssid:'Another demo network'},{wifi_password:'demo-only-password'},{mqtt_uri:'mqtt://another.demo:1883'},{mqtt_password:'demo-only-password'},{meter_baud:19200},{relay_board_enabled:true},{evu_input_enabled:true},{shell_rs485_interface:1,shell_rs485_host:'192.0.2.20:8899'},{mqtt_input_source:2}]){
    const d=await configuredDemo();assert.equal((await d.post('/api/config',change)).reboot_required,true);
    const status=await d.read('/api/status');assert.equal(status.reboot_required,true);assert.equal(status.enabled,false);
    const live=await d.post('/api/config',{price_kwh:.3});assert.equal(live.reboot_required,true);assert.equal(live.restart_needed,false);assert.equal(live.applied,true);
    await d.post('/api/reboot',{});assert.equal((await d.read('/api/status')).reboot_required,false);
    assert.equal((await d.post('/api/config',change)).reboot_required,false);
  }
  for(const change of [{price_kwh:.3},{solar_price_kwh:.09},{battery_reserve_soc:50},{battery_cloud_limit_w:3000},{pv_priority:1},{vehicle_soc_enabled:false},{charge_plan_enabled:true},{zero_feed_enabled:false},{zero_reserve_w:-100},{grid_limit_a:35},{max_charge_a:15},{grid_guard_enabled:true},{evu_limit_a:12},{house_meter_host:'meter.demo.local'},{house_power_path:'StatusSNS.Power'},{huawei_enabled:true},{huawei_host:'inverter.demo.local'},{huawei_unit_id:2},{huawei_battery:true},{huawei_pv:true},{shell_limits_auto:true},{shell_setup_host:'shell.demo.local'}]){
    const d=await configuredDemo();assert.equal((await d.post('/api/config',change)).reboot_required,false);assert.equal((await d.read('/api/status')).enabled,true);
  }
  const d=await configuredDemo();await d.post('/api/config',{mqtt_input_source:2});await d.post('/api/reboot',{});
  assert.equal((await d.post('/api/config',{opendtu_prefix:'another-dtu'})).reboot_required,true);
  const profile=demo();await profile.post('/api/config',{basic_mode:true,zero_feed_enabled:false});
  await profile.post('/api/reboot',{});
  assert.equal((await profile.post('/api/config',{basic_mode:false})).reboot_required,false,'showing extra functions alone needs no restart');
  console.log('PASS: demo WLAN/password and connection restarts, live prices/battery, pending restart and unchanged saves');
})().catch(error=>{console.error(error);process.exit(1);});
