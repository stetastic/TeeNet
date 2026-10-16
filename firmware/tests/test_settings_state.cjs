const fs=require('node:fs'),vm=require('node:vm'),assert=require('node:assert/strict');
const source=fs.readFileSync('main/dashboard.js','utf8');
function part(start,end){const a=source.indexOf(start),b=source.indexOf(end,a);assert(a>=0&&b>a);return source.slice(a,b);}
const callbacks={},pending=[],requests=[];
function field(name,value,type='number'){return {name,value:String(value),type,checked:!!value,disabled:false,dataset:{},hasAttribute(){return false;},closest(){return null;},checkValidity(){return true;}};}
const reserve=field('battery_reserve_soc',35),price=field('price_grid_eur_kwh',.25),verified=field('control_verified',true,'checkbox');
const elements=[reserve,price,verified];for(const el of elements)elements[el.name]=el;
elements.phase_actuator_type={value:'0'};elements.namedItem=name=>elements[name];
const nodes={'config-form':{elements,addEventListener(n,f){callbacks[n]=f;}},mode:{value:'pv'},'battery-reserve-slider':{value:'70'},'clear-wifi-password':{checked:false},'config-note':{},'battery-reserve-view':{}};
let backend={battery_reserve_soc:35,price_grid_eur_kwh:.25,control_verified:true};
let fail=false;
const ctx=vm.createContext({Number,String,Object,Math,Error,
  configBaseline:{...backend},batterySliderDirty:true,reserveQueued:true,busy:false,online:true,configSaving:false,settingsRebootPending:false,
  window:{teennetFeatures:{saved(){}}},$:id=>nodes[id],
  updateSaveStatus(){return false;},updateUnsavedFunctions(){},writes(){},pollStatus:async()=>{},toast(){},secretPlaceholders(){},updateBatterySlider(){},updateMeterFields(){},updateExpertMode(){},updateEquipment(){},
  action(work){const p=work();pending.push(p);return p;},
  async api(path,data){assert.equal(path,'/api/config');if(data===undefined)return {...backend};if(fail)throw Error('offline');requests.push(data);Object.assign(backend,data);return {reboot_required:false};}
});
vm.runInContext(part('async function loadConfig(){','function updateBatterySlider()'),ctx);
vm.runInContext(part('function applyReserveOnRelease(){',"$('battery-reserve-slider').addEventListener('change'"),ctx);
vm.runInContext(part('function validateConfigForm()',"$('reboot').addEventListener"),ctx);
async function complete(){await Promise.all(pending.splice(0));}
(async()=>{
  vm.runInContext('applyReserveOnRelease()',ctx);await complete();
  assert.equal(reserve.value,'70');assert.equal(ctx.configBaseline.battery_reserve_soc,70);
  price.value='.30';callbacks.submit({preventDefault(){}});await complete();
  assert(!('battery_reserve_soc' in requests.at(-1)),'saving a tariff must not restore an old reserve');
  assert.equal(backend.battery_reserve_soc,70);assert.equal(ctx.configSaving,false);
  reserve.value='45';nodes['battery-reserve-slider'].value='60';ctx.reserveQueued=true;
  vm.runInContext('applyReserveOnRelease()',ctx);await complete();
  assert.equal(reserve.value,'45','deliberately edited settings are preserved until save');
  backend.control_verified=false;await vm.runInContext('loadConfig()',ctx);
  assert.equal(verified.checked,false,'read back normalized server settings');
  elements.phase_actuator_type.value='1';nodes['phase-shelly-auth']={value:'0'};ctx.configBaseline.phase_shelly_password_set=true;callbacks.submit({preventDefault(){}});await complete();assert.equal(requests.at(-1).phase_shelly_password,'','Selecting no Shelly authentication clears the stored password');
  fail=true;callbacks.submit({preventDefault(){}});await assert.rejects(complete(),/offline/);
  assert.equal(ctx.configSaving,false,'failed save releases editing lock');
  console.log('PASS: reserve survives tariff save, pending edits retained, canonical settings loaded, failed saves unlock');
})().catch(error=>{console.error(error);process.exitCode=1;});
