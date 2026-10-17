(()=>{
 const dialog=document.getElementById('terms-dialog'),confirm=document.getElementById('terms-confirm'),accept=document.getElementById('terms-accept'),message=document.getElementById('terms-message');
 const selected=document.getElementById('terms-telemetry'),choice=document.getElementById('telemetry-enabled'),note=document.getElementById('telemetry-status');
 let pending=false,saving=false,revision='',filled=false,saveError='';
 function update(){
  if(!state||typeof state.terms_accepted!=='boolean')return;
  revision=state.terms_revision;
  if(!filled){confirm.checked=state.terms_accepted;selected.checked=true;filled=true;}
  if(!state.terms_accepted||state.telemetry_decided===false){
   if(!dialog.open)dialog.showModal();
   document.querySelectorAll('[data-write],#config-form input,#config-form select,#apply-control,#current,#battery-reserve-slider,#evcc-save').forEach(el=>el.disabled=true);
  }else if(dialog.open)dialog.close();
  accept.disabled=pending||!online||!confirm.checked||!revision;
  if(!saving){choice.checked=state.telemetry_enabled===true;choice.disabled=!online||pending||!state.terms_accepted;}
  if(!saving)note.textContent=saveError||(document.documentElement.dataset.demo==='true'?'Simulation: Diese Demo sendet keine Lebenszeichen.':
   state.telemetry_delete_pending?'Ausgeschaltet · Löschanforderung wird beim nächsten Kontakt gesendet.':
   !state.telemetry_enabled?'Ausgeschaltet · keine Lebenszeichen.':
   state.telemetry_last_ok?'Aktiv · letztes Lebenszeichen vor '+Math.max(0,Math.round(state.telemetry_last_age_s/60))+' Minuten.':'Aktiv · wartet auf Internetverbindung, gültige Uhrzeit oder das nächste Lebenszeichen.');
 }
 async function save(enabled){await api('/api/telemetry',{enabled,revision:state.telemetry_revision||'2026-10-16.1'});}
 dialog.addEventListener('cancel',event=>event.preventDefault());
 confirm.addEventListener('change',update);
 accept.addEventListener('click',async()=>{
  if(pending||!confirm.checked||!revision||!online)return;
  pending=true;update();message.textContent='Bestätigung wird gespeichert …';
  try{if(!state.terms_accepted)await api('/api/terms',{accepted:true,revision});await save(selected.checked);await pollStatus();writes();message.textContent='';}
  catch(error){message.textContent=error.message;}
  finally{pending=false;update();}
 });
 choice.addEventListener('input',event=>event.stopPropagation());
 choice.addEventListener('change',async event=>{
  event.stopPropagation();if(saving)return;saving=true;saveError='';choice.disabled=true;note.textContent='Auswahl wird gespeichert …';
  try{await save(choice.checked);await pollStatus();}catch(error){saveError=error.message;}
  finally{saving=false;update();}
 });
 window.addEventListener('teenet-status',update);window.teenetTerms={updateWrites:update};
})();
