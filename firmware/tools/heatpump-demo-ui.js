/* Read-only heat-pump settings; simulation controls exist only in the demo. */
(() => {
 'use strict';
 const form=document.getElementById('config-form'),card=document.getElementById('heatpump-settings');
 if(!form||!card||form.dataset.heatpumpUiReady)return;
 form.dataset.heatpumpUiReady='true';
 const field=name=>form.elements.namedItem(name),el=id=>document.getElementById(id);
 let savedStatus=null;
 function refresh(){
  const basic=field('basic_mode').value==='true',enabled=!basic&&field('heatpump_enabled').checked;
  if(basic)field('heatpump_enabled').checked=false;
  card.hidden=!enabled;
  const mqtt=field('heatpump_source').value==='mqtt';
  el('heatpump-shelly-fields').hidden=mqtt;
  el('heatpump-mqtt-fields').hidden=!mqtt;
  field('heatpump_host').required=enabled&&!mqtt;
  field('heatpump_input_topic').required=enabled&&mqtt;
  const publish=field('heatpump_publish_enabled').checked;
  el('heatpump-output-field').hidden=!publish;
  field('heatpump_output_topic').required=enabled&&publish;
  if(el('heatpump-demo-apply'))el('heatpump-demo-apply').disabled=!savedStatus?.heatpump_enabled;
 }
 form.addEventListener('change',event=>{
  if(event.target===field('basic_mode')&&event.target.value==='true')field('heatpump_enabled').checked=false;
  if(event.target===field('heatpump_enabled')&&event.target.checked)card.open=true;
  if(['heatpump_source','heatpump_enabled'].includes(event.target.name)&&field('heatpump_enabled').checked&&field('heatpump_source').value==='mqtt')field('mqtt_enabled').checked=true;
  refresh();
 },true);
 window.addEventListener('teenet-status',event=>{
  savedStatus=event.detail;
  const live=el('heatpump-live-state');
  if(live)live.textContent=!savedStatus.heatpump_enabled?'Nach Aktivierung speichern und neu starten.':savedStatus.heatpump_ok?`Aktuell: ${Math.round(savedStatus.heatpump_w).toLocaleString('de-DE')} W`:'Kein aktueller Messwert. Quelle und Verbindung prüfen.';
  const status=el('heatpump-demo-result'),output=el('heatpump-demo-output');
  if(!status||!output){refresh();return;}
  status.textContent=!savedStatus.heatpump_enabled?'Funktion zuerst aktivieren und speichern.':
   savedStatus.heatpump_ok?`Simulierter Messwert: ${Math.round(savedStatus.heatpump_w).toLocaleString('de-DE')} W`:'Kein aktueller Messwert.';
  const published=Number.isFinite(savedStatus.heatpump_published_w);
  output.hidden=!savedStatus.heatpump_enabled||!field('heatpump_publish_enabled').checked;
  output.replaceChildren();
  if(!output.hidden){
   output.append('MQTT-Ausgang (simuliert): ');
   const topic=document.createElement('code');topic.textContent=savedStatus.heatpump_mqtt_topic||field('heatpump_output_topic').value;
   output.append(topic, published?` → ${Math.round(savedStatus.heatpump_published_w)} W`:' → kein gültiger Messwert oder MQTT deaktiviert');
  }
  refresh();
 });
 window.addEventListener('teenet:heatpump-config',()=>setTimeout(refresh,0));
 el('heatpump-demo-apply')?.addEventListener('click',async()=>{
  const value=el('heatpump-demo-watts'),button=el('heatpump-demo-apply'),note=el('heatpump-demo-result');
  if(!value.reportValidity())return;
  button.disabled=true;
  try{
   const response=await fetch('/api/demo/heatpump',{method:'POST',headers:{'Content-Type':'application/json'},body:JSON.stringify({power_w:Number(value.value),valid:!el('heatpump-demo-missing').checked})});
   const result=await response.json();
   if(!response.ok||result.ok===false)throw new Error(result.error||'Testwert nicht übernommen.');
   if(typeof pollStatus==='function')await pollStatus();
   else note.textContent='Testwert übernommen. Die Anzeige aktualisiert sich gleich.';
  }catch(error){note.textContent=error.message;}
  finally{refresh();}
 });
 el('heatpump-demo-watts')?.addEventListener('keydown',event=>{if(event.key==='Enter'){event.preventDefault();el('heatpump-demo-apply').click();}});
 refresh();
})();
