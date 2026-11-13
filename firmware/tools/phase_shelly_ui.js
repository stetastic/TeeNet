/* Separate Shelly actuator setup; meter searches keep their own results. */
(() => {
 let scanning=false,devices=[],pollTimer,lastBaseline;
 const f=$('config-form').elements,select=$('phase-shelly-found'),note=$('phase-shelly-scan-status'),feedback=$('phase-feedback-choice'),connection=$('phase-shelly-connection'),actuator=$('phase-actuator-choice');
 const source=()=>Number(f.phase_feedback_source.value||2);
 const choice=()=>source()===1?'voltage':'none';
 let voltageManual=false;
 function voltageMeter(){
  const picker=$('phase-voltage-meter'),host=f.wallbox_meter_host?.value.trim()||'',available=f.wallbox_meter_type?.value==='shelly_gen2'&&!!host;
  picker.replaceChildren();
  if(available){const option=document.createElement('option');option.value='wallbox';option.textContent=`Wallboxzähler · Shelly 3EM · ${host} · Modbus TCP`;picker.append(option);}
  const custom=document.createElement('option');custom.value='manual';custom.textContent='Anderen Shelly 3EM angeben';picker.append(custom);
  const same=available&&!voltageManual&&f.phase_voltage_host.value===host&&f.phase_voltage_protocol.value==='0';picker.value=same?'wallbox':'manual';
  $('phase-voltage-protocol-label').hidden=same;$('phase-voltage-host-label').hidden=same;
  const reversed=!available||f.phase_voltage_host.value!==host||f.wallbox_shelly_reverse?.value!=='false';
  $('phase-voltage-wiring-note').textContent=reversed?'Shelly C/L3 → Wallbox L1, B/L2 → L2, A/L1 → L3.':'Shelly A/L1 → Wallbox L1, B/L2 → L2, C/L3 → L3.';
 }
 function update(){
  const kind=Number(f.phase_actuator_type.value),remote=kind===1||kind===3,mqtt=kind===2;
  const baseline=typeof configBaseline==='undefined'?null:configBaseline;
  if(baseline&&baseline!==lastBaseline){$('phase-shelly-auth').value=baseline.phase_shelly_password_set?'1':'0';connection.value=kind===3?'3':'1';lastBaseline=baseline;}
  feedback.value=choice();actuator.value=remote?'1':String(kind);
  for(const option of Array.from(f.phase_shelly_model.options||[]))option.disabled=false;
  const modbusOption=Array.from(connection.options||[]).find(option=>option.value==='3');
  if(modbusOption){const supported=['4','12'].includes(f.phase_shelly_model.value);modbusOption.hidden=!supported;modbusOption.disabled=!supported;}
  $('phase-shelly-settings').hidden=!remote;$('phase-actuator-label').hidden=false;
  $('phase-mqtt-normal').hidden=!mqtt;$('phase-voltage-settings').hidden=source()!==1;
  $('phase-feedback-settings').hidden=false;
  $('phase-feedback-heading').textContent='Stellungsprüfung · optional';
  $('phase-feedback-description').textContent=source()===1?'Die Phasenspannungen bestätigen die Schützstellung.':'';
  $('phase-feedback-description').hidden=!$('phase-feedback-description').textContent;
  f.relay1_mode.value=f.phase_switch_enabled.checked?'3':'0';
  f.phase_shelly_host.required=remote&&f.phase_switch_enabled.checked;
  f.phase_voltage_host.required=source()===1&&f.phase_switch_enabled.checked;
  const password=$('phase-shelly-auth').value==='1';
  $('phase-shelly-auth-settings').hidden=kind===3;$('phase-shelly-password-label').hidden=!password;
  f.phase_shelly_password.disabled=kind===3||!password;
  f.phase_shelly_password.required=kind===1&&f.phase_switch_enabled.checked&&password&&!baseline?.phase_shelly_password_set;
  const gp={relay1_pin:state?.hardware_id==='andreas-testboard'?7:12,...state?.gpio_pins,...baseline};
  $('phase-connection-note').hidden=remote||mqtt;
  $('phase-connection-note').textContent=`GPIO${gp.relay1_pin}: Schützausgang · High schaltet das Relais ein.`;
  $('phase-contactor-migration').hidden=!state?.phase_contactor_reconfigure;
  voltageMeter();updateLive();
 }
 function updateLive(){
  const kind=Number(f.phase_actuator_type.value),baseline=typeof configBaseline==='undefined'?null:configBaseline,live=$('phase-contact-status');
  const diagnostic=$('phase-shelly-diagnostic');
  diagnostic.hidden=!state?.phase_switch_enabled||![1,3].includes(kind)||kind!==state?.phase_actuator_type||state?.phase_shelly_config_ok!==false||!!state?.reboot_required;
  diagnostic.textContent=diagnostic.hidden?'':'Shelly nicht bereit: Modell, Adresse und Anmeldung prüfen. Am Shelly Eingang „Detached“, Einschaltzustand „Aus“ und Timer aus einstellen.';
  if(kind===3||$('phase-shelly-auth').value!=='1')f.phase_shelly_password.disabled=true;
  live.hidden=!state?.phase_switch_enabled||kind!==state.phase_actuator_type;
  live.hidden ||= source()!==1;
  if(live.hidden)return;
  const changed=baseline&&['phase_feedback_source','phase_voltage_host','phase_voltage_protocol','phase_shelly_model','phase_shelly_true_three_phase','phase_shelly_host'].some(name=>baseline[name]!==undefined&&String(baseline[name])!==f[name].value);
  if(changed||state.reboot_required){live.textContent=state.reboot_required?'Neue Prüfung nach dem Neustart aktiv.':'Auswahl noch nicht gespeichert.';return;}
  const observed=Number(source()===1?state.phase_voltage_position??(state.phase_voltage_ok?state.phase_feedback:0):state.phase_feedback),position=observed===1?'Einphasig bestätigt':observed===3?'Dreiphasig bestätigt':'Stellung nicht bestätigt';
  if(source()===2){live.textContent='Ohne Rückmeldung · Stellung nicht geprüft.';return;}
  if(source()===1){
   const volts=state.phase_voltage_v||[];
   let detail=position;
   if(volts.length===3&&volts.every(Number.isFinite)&&volts[0]<180)detail='Dauerphase L1 ohne Spannung · Phasenzuordnung prüfen.';
   else if(observed&&[1,3].includes(kind)&&state.phase_shelly_config_ok&&state.phase_shelly_status_ok&&Number(state.phase_feedback)===0)detail+=' · Shelly-Schaltzustand passt nicht dazu.';
   live.textContent=volts.map((v,i)=>`L${i+1}: ${Number.isFinite(v)?Math.round(v)+' V':'—'}`).join(' · ')+' · '+detail;return;
  }

 }
 function normalizeChoice(event){
  let kind=Number(f.phase_actuator_type.value);
  if(event?.target===connection){kind=Number(connection.value);f.phase_actuator_type.value=String(kind);}
  if(kind===3&&!['4','12'].includes(f.phase_shelly_model.value)){kind=1;f.phase_actuator_type.value='1';}
  connection.value=kind===3?'3':'1';
  if(source()!==1)f.phase_feedback_source.value='2';
  update();
 }
 function applyFeedback(value){
  f.phase_feedback_source.value=value==='voltage'?'1':'2';
  if(value==='voltage'&&!f.phase_voltage_host.value&&f.wallbox_meter_type?.value==='shelly_gen2'&&f.wallbox_meter_host?.value){f.phase_voltage_host.value=f.wallbox_meter_host.value;f.phase_voltage_protocol.value='0';voltageManual=false;}
  normalizeChoice();f.phase_feedback_source.dispatchEvent(new Event('change',{bubbles:true}));
 }
 feedback.addEventListener('change',()=>applyFeedback(feedback.value));
 $('phase-voltage-meter').addEventListener('change',()=>{voltageManual=$('phase-voltage-meter').value==='manual';if(!voltageManual){f.phase_voltage_host.value=f.wallbox_meter_host.value;f.phase_voltage_protocol.value='0';f.phase_voltage_host.dispatchEvent(new Event('change',{bubbles:true}));}voltageMeter();});
 for(const name of ['wallbox_meter_type','wallbox_meter_host','phase_voltage_host','phase_voltage_protocol'])f[name]?.addEventListener('change',voltageMeter);
 function render(result){
  if(result.family!=='shelly_switch')throw Error('Andere Gerätesuche läuft. Bitte später erneut versuchen.');
  devices=result.devices||[];select.replaceChildren();
  const first=document.createElement('option');first.value='';first.textContent='Shelly auswählen';select.append(first);
  devices.forEach((device,index)=>{const option=document.createElement('option');option.value=String(index);option.disabled=!device.compatible;option.textContent=`${device.host} · ${device.name}${device.protected?' · Passwortschutz':''}${device.compatible?' · Hardware noch nicht getestet':' · nicht unterstützt'}`;select.append(option);});select.hidden=!devices.length;
  note.textContent=result.running?`Suche nach Shelly-Schaltgeräten · ${result.progress} %`:`${devices.filter(d=>d.compatible).length} passende Shelly gefunden.`;
 }
 async function poll(){try{const result=await api('/api/meters/scan');render(result);if(result.running){pollTimer=setTimeout(poll,1500);return;}}catch(error){note.textContent=error.message;}scanning=false;$('phase-shelly-scan').disabled=!online||busy;}
 $('phase-shelly-scan').addEventListener('click',async()=>{if(scanning||!online||busy)return;scanning=true;$('phase-shelly-scan').disabled=true;clearTimeout(pollTimer);try{const result=await api('/api/meters/scan',{family:'shelly_switch'});render(result);if(result.running)pollTimer=setTimeout(poll,1500);else{scanning=false;$('phase-shelly-scan').disabled=!online||busy;}}catch(error){note.textContent=error.message;scanning=false;$('phase-shelly-scan').disabled=!online||busy;}});
 select.addEventListener('change',()=>{const device=devices[Number(select.value)];if(select.value===''||!device?.compatible)return;f.phase_shelly_host.value=device.host;if(Number.isInteger(device.profile)&&device.profile<=12)f.phase_shelly_model.value=String(device.profile);$('phase-shelly-auth').value=device.protected?'1':'0';normalizeChoice();f.phase_shelly_host.dispatchEvent(new Event('change',{bubbles:true}));});
 actuator.addEventListener('change',()=>{f.phase_actuator_type.value=actuator.value==='1'?(connection.value==='3'&&['4','12'].includes(f.phase_shelly_model.value)?'3':'1'):actuator.value;normalizeChoice();f.phase_actuator_type.dispatchEvent(new Event('change',{bubbles:true}));});
 for(const name of ['phase_actuator_type','phase_shelly_model'])f[name].addEventListener('change',normalizeChoice);
 connection.addEventListener('change',event=>{normalizeChoice(event);f.phase_actuator_type.dispatchEvent(new Event('change',{bubbles:true}));});
 $('phase-shelly-auth').addEventListener('change',update);
 window.addEventListener('teenet-status',updateLive);
 window.teenetShellyPhase={update,updateLive};update();
})();
