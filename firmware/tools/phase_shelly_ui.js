/* Separate Shelly actuator setup; meter searches keep their own results. */
(() => {
 let scanning=false,devices=[],pollTimer,lastBaseline;
 const f=$('config-form').elements,select=$('phase-shelly-found'),note=$('phase-shelly-scan-status'),feedback=$('phase-feedback-choice'),connection=$('phase-shelly-connection');
 function update(){
  const kind=Number(f.phase_actuator_type.value),remote=kind===1||kind===3,mqtt=kind===2;
  const baseline=typeof configBaseline==='undefined'?null:configBaseline;
  if(baseline&&baseline!==lastBaseline){$('phase-shelly-auth').value=baseline.phase_shelly_password_set?'1':'0';connection.value=Number(baseline.phase_actuator_type)===3?'3':'1';feedback.value=f.phase_feedback_count.value==='2'?'2':f.phase_feedback_closed_is_single.value==='true'?'1-single':'1';$('phase-choice-note').hidden=true;$('phase-choice-note').textContent='';lastBaseline=baseline;}
  if(!connection.value)connection.value=kind===3?'3':'1';
  if(!feedback.value)feedback.value='1';
  const none=mqtt,configured=!!state?.phase_mqtt_allowed||baseline?.phase_mqtt_output_enabled===true;

  for(const option of Array.from(f.phase_shelly_model.options||[]))option.disabled=false;
  const modbusOption=Array.from(connection.options||[]).find(option=>option.value==='3');
  if(modbusOption){const supported=['4','12'].includes(f.phase_shelly_model.value);modbusOption.hidden=!supported;modbusOption.disabled=!supported;}
  const inputs=[2,2,1,1,1,2,2,2,3,4,1,1,1][Number(f.phase_shelly_model.value)]||1;

  for(const option of Array.from(feedback.options||[]))option.disabled=remote&&((option.value==='2'&&inputs===1)||(option.value!=='2'&&f.phase_shelly_pulse.value==='true'));
  for(const option of Array.from(f.phase_shelly_pulse.options||[]))option.disabled=option.value==='true'&&(kind===3||inputs===1);
  $('phase-shelly-settings').hidden=!remote||none;
  $('phase-actuator-label').hidden=none&&configured;
  $('phase-feedback-settings').hidden=none;
  $('phase-feedback-heading').textContent=remote?'Rückmeldung über Shelly':'Rückmeldung am ESP';
  $('phase-feedback-description').textContent=remote?'Hilfskontakte des Schützes an die Shelly-Eingänge anschließen.':'Potentialfreie Hilfskontakte des Schützes an die ESP-Eingänge anschließen.';
  f.phase_feedback_enabled.closest('label').hidden=true;
  $('phase-connection-note').hidden=remote||none;
  f.phase_shelly_host.required=remote&&!none&&f.phase_switch_enabled.checked;
  /* Previewing the protected option must not dirty ordinary saved settings. */
  f.phase_feedback_enabled.checked=!mqtt;f.relay1_mode.value=f.phase_switch_enabled.checked?'3':'0';
  const password=$('phase-shelly-auth').value==='1';
  $('phase-shelly-auth-settings').hidden=kind===3;
  $('phase-shelly-password-label').hidden=!password;
  f.phase_shelly_password.disabled=kind===3||!password;
  f.phase_shelly_password.required=kind===1&&!none&&f.phase_switch_enabled.checked&&password&&!baseline?.phase_shelly_password_set;
  $('phase-shelly-polarity').hidden=f.phase_shelly_pulse.value==='true';
  const pulseNote=$('phase-pulse-support');
  pulseNote.hidden=!remote||none;
  pulseNote.textContent=kind===3?'Modbus: nur Ein/Aus. Für Impulse Pro 1 oder Pro 2 über HTTP wählen.':
   inputs===1?'Dieses Modell hat einen Rückmeldeeingang. Impulse benötigen Pro 1 oder Pro 2 mit zwei Kontakten.':
   f.phase_shelly_pulse.value==='true'?'TeeNet startet den Impuls. Der Shelly schaltet nach 0,5 s aus. Zwei Rückmeldekontakte erforderlich.':'';
  pulseNote.hidden=!remote||none||!pulseNote.textContent;
  const gp={relay1_pin:12,phase_feedback_pin:13,phase_feedback_second_pin:14,...state?.gpio_pins,...baseline};
  $('phase-connection-note').innerHTML=`GPIO${gp.relay1_pin}: Relais · 3,3 V / High schaltet ein · GPIO${gp.phase_feedback_pin}: Kontakt 1${Number(f.phase_feedback_count.value)===2?` · GPIO${gp.phase_feedback_second_pin}: Kontakt 2`:''}. <a href="/guide#relais-phasen" target="_blank" rel="noopener">Anschluss</a>`;
  const mapping=f.phase_feedback_closed_is_single;
  $('phase-feedback-single-mapping').hidden=true;
  const closedSingle=mapping.value==='true',closedText=closedSingle?'einphasig':'dreiphasig',openText=closedSingle?'dreiphasig':'einphasig';
  const pins=$('phase-feedback-pin-note');pins.hidden=none;
  pins.textContent=remote?(Number(f.phase_feedback_count.value)===2?
   'SW1 (Eingang 0) geschlossen: dreiphasig.\nSW2 (Eingang 1) geschlossen: einphasig.\nGenau ein Kontakt muss geschlossen sein.':
   `SW1 (Eingang 0) geschlossen: ${closedText}.\nOffen: ${openText}. Eingang nicht invertieren.`):
   Number(f.phase_feedback_count.value)===2?
   `GPIO${gp.phase_feedback_pin} gegen GND geschlossen: dreiphasig. GPIO${gp.phase_feedback_second_pin} gegen GND geschlossen: einphasig. Genau ein Kontakt muss geschlossen sein.`:
   `GPIO${gp.phase_feedback_pin} gegen GND geschlossen: ${closedText}. Offen: ${openText}. Nur potentialfreie Kontakte verwenden.`;
  $('phase-shelly-setup-note').textContent=kind===3?'Am Shelly: Modbus aktivieren, Eingang entkoppeln (Detached), Einschaltzustand Aus, Timer aus. HTTP bleibt für die Prüfung nötig.':
   f.phase_shelly_pulse.value==='true'?'Am Shelly: Eingang entkoppeln (Detached), Einschaltzustand Aus, Auto-Aus 0,5 s; Auto-Ein aus.':
   'Am Shelly: Eingang entkoppeln (Detached), Einschaltzustand Aus, Timer aus.';
  const option=Array.from(f.phase_actuator_type.options||[]).find(o=>o.value==='2');
  if(option){option.hidden=true;option.disabled=true;}
  updateLive();
 }
 /* Status polling must not rewrite unsaved configuration fields. */
 function updateLive(){
  const kind=Number(f.phase_actuator_type.value),none=kind===2,baseline=typeof configBaseline==='undefined'?null:configBaseline;
  const runtimeAllowed=!!state?.phase_mqtt_allowed,configured=runtimeAllowed||baseline?.phase_mqtt_output_enabled===true;
  $('phase-actuator-label').hidden=none&&configured;
  const outputNote=$('phase-mqtt-profile-note');outputNote.hidden=!none;
  if(none)outputNote.textContent=configured?
   (runtimeAllowed?`${['MQTT','Shelly Modbus TCP','Shelly HTTP ohne Passwort','Shelly HTTP mit Passwort'][state.phase_output_transport||0]}: ${typeof state.phase_mqtt_output_single==='boolean'?`letzter Befehl ${state.phase_mqtt_output_single?'einphasig':'dreiphasig'}`:'noch kein Befehl bestätigt'} · keine Schützrückmeldung.`:'Ohne Rückmeldung gespeichert · nach dem Neustart aktiv.'):
   'Ohne Rückmeldung: Freigabe im Expertenmodus erforderlich.';
  if(none){const link=document.createElement('a');link.href='#expert-settings';link.textContent=' Im Expertenmodus ändern';outputNote.append(link);}
  const contacts=$('phase-contact-status');
  contacts.hidden=none||!state?.phase_switch_enabled||kind!==state.phase_actuator_type;
  if(!contacts.hidden){
   const changed=baseline&&['phase_feedback_count','phase_feedback_closed_is_single','phase_shelly_model','phase_shelly_pulse','phase_shelly_true_three_phase','phase_shelly_host'].some(name=>String(baseline[name])!==f[name].value);
   if(changed||state.reboot_required){contacts.textContent=state.reboot_required?'Rückmeldung nach dem Neustart prüfen.':'Neue Zuordnung noch nicht gespeichert.';return;}
   const contactText=value=>typeof value==='boolean'?(value?'geschlossen':'offen'):'unbekannt';
   const observed=Number(state.phase_feedback);
   contacts.textContent=`Kontakt 1: ${contactText(state.phase_contact1_closed)}${Number(state.phase_feedback_count)===2?` · Kontakt 2: ${contactText(state.phase_contact2_closed)}`:''} · ${observed===1?'Einphasig bestätigt':observed===3?'Dreiphasig bestätigt':'Schützstellung nicht bestätigt'}`;
  }
 }
 function render(result){
  if(result.family!=='shelly_switch')throw Error('Andere Gerätesuche läuft. Bitte später erneut versuchen.');
  devices=result.devices||[];select.replaceChildren();
  const first=document.createElement('option');first.value='';first.textContent='Shelly auswählen';select.append(first);
  devices.forEach((device,index)=>{
   const option=document.createElement('option');option.value=String(index);option.disabled=!device.compatible;
   option.textContent=`${device.host} · ${device.name}${device.protected?' · Passwortschutz':''}${device.compatible?' · Hardware noch nicht getestet':' · nicht unterstützt'}`;
   select.append(option);
  });select.hidden=!devices.length;
  note.textContent=result.running?`Suche nach Shelly-Schaltgeräten · ${result.progress} %`:
   `${devices.filter(d=>d.compatible).length} passende Shelly gefunden.${result.partial?' Suche vorzeitig beendet.':''} ${devices.some(d=>d.protected)?'Geschützte Geräte werden angezeigt; Passwort unten eingeben.':''}`;
 }
 async function poll(){
  try{const result=await api('/api/meters/scan');render(result);if(result.running){pollTimer=setTimeout(poll,1500);return;}}
  catch(error){note.textContent=error.message;}
  scanning=false;$('phase-shelly-scan').disabled=!online||busy;
 }
 $('phase-shelly-scan').addEventListener('click',async()=>{
  if(scanning||!online||busy)return;
  scanning=true;$('phase-shelly-scan').disabled=true;clearTimeout(pollTimer);
  try{const result=await api('/api/meters/scan',{family:'shelly_switch'});render(result);if(result.running)pollTimer=setTimeout(poll,1500);else scanning=false;}
  catch(error){scanning=false;note.textContent=error.message;}
  if(!scanning)$('phase-shelly-scan').disabled=!online||busy;
 });
 select.addEventListener('change',()=>{
  const device=devices[Number(select.value)];if(select.value===''||!device?.compatible)return;
  f.phase_shelly_host.value=device.host;
  if(Number.isInteger(device.profile)&&device.profile<=12)f.phase_shelly_model.value=String(device.profile);
  $('phase-shelly-auth').value=device.protected?'1':'0';normalizeChoice();update();
  f.phase_shelly_host.dispatchEvent(new Event('change',{bubbles:true}));
 });
 // Normalize only explicit user choices, never saved data while rendering or polling.
 function normalizeChoice(event){
  let kind=Number(f.phase_actuator_type.value),remote=kind===1||kind===3;
  const notice=$('phase-choice-note');notice.textContent='';notice.hidden=true;
  if(event?.target===connection){kind=Number(connection.value);f.phase_actuator_type.value=String(kind);remote=kind===1||kind===3;}
  if((event?.target===connection||event?.target?.name==='phase_actuator_type')&&kind===3){if(!['4','12'].includes(f.phase_shelly_model.value))f.phase_shelly_model.value='4';notice.textContent='Modbus TCP gewählt: Gen4-Shelly mit einem Rückmeldekontakt.';}
  if(kind===3&&!['4','12'].includes(f.phase_shelly_model.value)){f.phase_actuator_type.value='1';connection.value='1';kind=1;notice.textContent='Dieses Modell unterstützt hier HTTP. Die Verbindung wurde auf HTTP gesetzt.';}
  else connection.value=kind===3?'3':'1';
  const inputs=[2,2,1,1,1,2,2,2,3,4,1,1,1][Number(f.phase_shelly_model.value)]||1;
  if(remote&&(kind===3||inputs===1))f.phase_shelly_pulse.value='false';
  if(remote&&inputs===1)f.phase_feedback_count.value='1';
  if(remote&&f.phase_shelly_pulse.value==='true')f.phase_feedback_count.value='2';
  if(f.phase_feedback_count.value==='2')f.phase_feedback_closed_is_single.value='false';
  feedback.value=f.phase_feedback_count.value==='2'?'2':f.phase_feedback_closed_is_single.value==='true'?'1-single':'1';
  notice.hidden=!notice.textContent;
 }
 for(const name of ['phase_actuator_type','phase_shelly_model','phase_shelly_pulse'])f[name].addEventListener('change',normalizeChoice);
 connection.addEventListener('change',event=>{normalizeChoice(event);f.phase_actuator_type.dispatchEvent(new Event('change',{bubbles:true}));});
 $('phase-shelly-auth').addEventListener('change',update);
 window.teenetShellyPhase={update,updateLive};update();
})();
