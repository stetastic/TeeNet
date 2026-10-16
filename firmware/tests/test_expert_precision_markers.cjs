const fs=require('node:fs'),vm=require('node:vm'),assert=require('node:assert/strict');
const source=fs.readFileSync('tools/expert_ui.js','utf8');
const schema=JSON.parse(fs.readFileSync('preview/expert-schema.json','utf8'));
const numberContext=vm.createContext({});
vm.runInContext(source.slice(source.indexOf(' function parameterMeta('),source.indexOf(' function readable(')),numberContext);
// Use actual float32 metadata, as returned by the embedded firmware.
for(const original of schema.parameters){
 const raw={...original,value:original.default,active:original.default};
 for(const key of ['min','max','step','default','value','active'])raw[key]=Math.fround(raw[key]);
 const meta=numberContext.parameterMeta(raw);
 for(const key of ['min','max','step','default'])assert.equal(meta[key],original[key],`${original.id}: ${key}`);
 assert.equal(meta.value,original.default);
 assert.equal(meta.active,original.default);
 assert(Math.abs((meta.value-meta.min)/meta.step-Math.round((meta.value-meta.min)/meta.step))<1e-8,`${original.id}: default fits browser step`);
}
const deficit=numberContext.parameterMeta({min:0,max:2,step:Math.fround(.1),value:.5});
assert.equal(deficit.step,.1);assert.equal((.5-deficit.min)/deficit.step,5);
assert.equal(numberContext.parameterMeta({value:Math.fround(.3)}).value,.3);

const nodes={};let draftModified=false;
function node(){const classes=new Set();return {classes,classList:{toggle(key,on){if(on)classes.add(key);else classes.delete(key);}},setAttribute(){},removeAttribute(){},replaceChildren(){draftModified=false;},querySelectorAll:()=>[],querySelector(selector){if(selector===':scope > summary')return this.summary??=node();return draftModified?{}:null;}};}
const el=id=>nodes[id]??=node();
let testDialogClosed=0;
const context={window:{teenetPhaseTest:{close(){testDialogClosed++;}}},el,pin:'4040',snapshot:null,rows:[{}],savedModified:true,lastMarkerState:null,state:{expert_parameters_modified:true},clearTimeout(){},timer:null,message(){},cancelPhaseMqtt(){},cancelPhaseAccess(){},update:()=>context.updateGroupMarkers()};
vm.createContext(context);
vm.runInContext(source.slice(source.indexOf(' function updateGroupMarkers()'),source.indexOf(' function update()')),context);
vm.runInContext(source.slice(source.indexOf(' function lock()'),source.indexOf(' function chargingActive()')),context);
const customized=()=>el('expert-settings').classes.has('has-modified-parameters');
context.updateGroupMarkers(); // Read current saved state before editing.
draftModified=true;context.updateGroupMarkers();assert(customized());
context.lock();assert(customized(),'saved deviation survives cleared rows and lock');
assert.equal(testDialogClosed,1,'Locking expert access closes an open contactor-test dialog');
assert(el('maintenance').classes.has('has-modified-parameters'),'outer heading remains marked');
assert.equal(context.pin,'');assert.equal(context.rows.length,0);
context.state={expert_parameters_modified:true};context.updateGroupMarkers();assert(customized(),'fresh status retains locked marker');
context.state={expert_parameters_modified:false};context.updateGroupMarkers();assert(!customized(),'restoring and saving defaults clears marker');
context.pin='4040';context.rows=[{}];draftModified=true;context.updateGroupMarkers();assert(customized());
context.lock();assert(!customized(),'discarded unsaved draft must not create a saved marker');
context.state={expert_parameters_modified:true};context.updateGroupMarkers();assert(customized(),'reload finds saved deviations without unlocking');
context.state={expert_parameters_modified:false,phase_mqtt_allowed:true};context.updateGroupMarkers();assert(!customized(),'phase permissions no longer belong to the expert parameter section');
context.state={expert_parameters_modified:false,phase_mqtt_allowed:false,phase_mqtt_true_three_phase:true};context.updateGroupMarkers();assert(!customized(),'phase contactor polarity is configured in the separate phase card');
context.pin='4040';context.rows=[];context.state={expert_parameters_modified:true};context.updateGroupMarkers();assert(customized(),'phase-only unlock does not hide saved parameter markers');
// A save followed by lock must not be overwritten by the previous status object.
context.pin='4040';context.state={expert_parameters_modified:false};context.updateGroupMarkers();
context.savedModified=true;context.lock();assert(customized());
assert(fs.readFileSync('main/app.c','utf8').includes('"expert_parameters_modified",expert_modified(&expert_saved)'));
console.log('PASS: float32 input precision and saved deviation markers through lock, reset, reload and unsaved draft discard');
