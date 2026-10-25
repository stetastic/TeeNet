const fs=require('node:fs'),vm=require('node:vm'),assert=require('node:assert/strict');
const dashboard=fs.readFileSync('main/dashboard.js','utf8');
function part(source,start,end){const from=source.indexOf(start),to=source.indexOf(end,from);assert(from>=0&&to>from);return source.slice(from,to);}
function field(name,value,type='text',attribute=''){
  return {name,value:String(value),checked:!!value,type,tagName:type==='select'?'SELECT':'INPUT',options:[{value:'0'},{value:'1'},{value:'2'}],dataset:{},
    hasAttribute(key){return key===attribute;},closest(){return null;},append(option){this.options.push(option);}};
}
const price=field('price_kwh',.25,'number'),reserve=field('battery_reserve_soc',35,'number');
const kind=field('phase_actuator_type',2,'select','data-number'),count=field('phase_feedback_count',1,'hidden','data-number');
const mapping=field('phase_feedback_closed_is_single',false,'select','data-boolean'),mqtt=field('mqtt_enabled',true,'checkbox');
mapping.value='false';
const fields=[price,reserve,kind,count,mapping,mqtt];fields.namedItem=name=>fields.find(input=>input.name===name);
for(const input of fields)fields[input.name]=input;
const nodes={'config-form':{elements:fields},'battery-reserve-slider':{value:''},'phase-shelly-auth':{value:'1'},'phase-feedback-choice':{value:'1'}};
let stored={price_kwh:.25,battery_reserve_soc:35,phase_actuator_type:2,phase_feedback_count:1,phase_feedback_closed_is_single:false,mqtt_enabled:true,phase_shelly_password_set:true,phase_mqtt_output_enabled:true};
let readGate=null;
const requests=[],context=vm.createContext({
  Number,String,Object,Array,Math,configBaseline:{...stored},batterySliderDirty:false,
  pin:'',snapshot:null,state:{phase_mqtt_allowed:true},phaseMqttDraft:null,
  $:id=>nodes[id],el:id=>nodes[id],window:{teenetShellyPhase:{update(){}}},same:(a,b)=>typeof a==='number'&&typeof b==='number'?Math.abs(a-b)<.0001:a===b,
  updateBatterySlider(){},updateMeterFields(){},updateExpertMode(){},updateEquipment(){},secretPlaceholders(){},
  updateSaveStatus(){},updateUnsavedFunctions(){},fillPhaseSettings(){},renew(){},pollStatus:async()=>{},
  async api(path,body){
    requests.push({path,body});
    if(path==='/api/config'){assert.equal(body,undefined);if(readGate)await readGate;return {...stored};}
    assert.equal(path,'/api/expert');assert.equal(body.action,'mqtt_phase_output');
    stored={...stored,phase_mqtt_output_enabled:body.enabled,phase_actuator_type:body.enabled?2:0};
    return {reboot_required:true};
  }
});
vm.runInContext(part(dashboard,'async function loadConfig(','function updateBatterySlider()'),context);
vm.runInContext(part(dashboard,' async function writePhaseMqtt(',' async function changePhaseMqtt('),context);
(async()=>{
  price.value='.38';reserve.value='45';mqtt.checked=false;kind.value='1';count.value='2';nodes['phase-feedback-choice'].value='2';nodes['phase-shelly-auth'].value='0';
  await context.writePhaseMqtt(false);
  assert.equal(price.value,'.38','revoking phase output preserves the unsaved tariff draft');
  assert.equal(reserve.value,'45','revoking phase output preserves the unsaved reserve draft');
  assert.equal(mqtt.checked,false,'checkbox drafts survive the protected configuration refresh');
  assert.equal(context.configBaseline.price_kwh,.25,'the persisted baseline remains authoritative');
  assert.equal(kind.value,0,'the protected operation itself determines the new actuator profile');
  assert.equal(count.value,'2');assert.equal(nodes['phase-feedback-choice'].value,'2','the matching feedback selector draft is preserved');
  assert.equal(nodes['phase-shelly-auth'].value,'0','an unsaved authentication selection is preserved');
  assert.equal(stored.price_kwh,.25,'ordinary drafts are never written implicitly');
  assert.equal(requests.length,2,'revocation stays available and only reloads the saved configuration');
  let release;readGate=new Promise(resolve=>{release=resolve;});
  const pending=context.writePhaseMqtt(false);await Promise.resolve();
  price.value='.42';release();await pending;
  assert.equal(price.value,'.42','edits made while the configuration refresh is in flight survive too');
  console.log('PASS: protected phase reconfiguration preserves unrelated form drafts and accepts fields changed by the server');
})().catch(error=>{console.error(error);process.exitCode=1;});
