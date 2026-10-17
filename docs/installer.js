const confirm=document.getElementById('install-confirm');
const terms=document.getElementById('install-terms-confirm');
const status=document.getElementById('install-status');
const installer=document.getElementById('web-install');
const connect=document.getElementById('install-connect');
const board=document.getElementById('install-board');
const confirmText=document.getElementById('install-confirm-text');
const hardware=document.getElementById('install-hardware');
const boardNote=document.getElementById('board-note');
let ready=false;
const sync=()=>{connect.disabled=!ready||!confirm.checked||!terms.checked;};
confirm.addEventListener('change',sync);terms.addEventListener('change',sync);
let version='';
function selectBoard(){
  const andreas=board.value==='andreas';
  confirm.checked=false;
  confirmText.textContent=andreas?'Mein Board ist das Andreas Testboard (AtomS3 Lite, 8 MB). Vorhandene Daten dürfen gelöscht werden.':'Mein Board ist ein ESP32-S3 N16R8. Vorhandene Daten auf diesem Board dürfen gelöscht werden.';
  hardware.textContent=andreas?'Andreas Testboard · AtomS3 Lite · 8 MB Flash':'ESP32-S3 N16R8 · 16 MB Flash';
  boardNote.textContent=andreas?'Testprofil für Andreas: TX6/RX5. Software geprüft; Hardwaretest steht noch aus.':'Erprobte Standardhardware mit 16 MB Flash.';
  if(version)installer.setAttribute('manifest',andreas?`/TeeNet/downloads/TeeNet-${version}-andreas-testboard-web-install.json`:`/TeeNet/downloads/TeeNet-${version}-web-install.json`);
  sync();
}
board.addEventListener('change',selectBoard);
function fail(message){status.textContent=message;status.dataset.error='true';confirm.disabled=true;terms.disabled=true;sync();}
async function setup(){
  if(!window.isSecureContext){fail('Bitte diese Seite über HTTPS öffnen. USB-Zugriff ist über unverschlüsseltes HTTP gesperrt.');return;}
  if(!('serial' in navigator)){fail('USB-Installation ist in diesem Browser nicht verfügbar. Bitte Chrome oder Edge am Computer verwenden.');return;}
  const controller=new AbortController(),timeout=setTimeout(()=>controller.abort(),12000);
  try{
    const response=await fetch('/TeeNet/downloads/web-install.json',{cache:'no-store',signal:controller.signal});
    if(!response.ok)throw new Error('Installationsdateien nicht erreichbar.');
    const manifest=await response.json();
    if(manifest.name!=='TeeNet'||!/^\d+\.\d+$/.test(manifest.version)||manifest.builds?.length!==1||manifest.builds[0].chipFamily!=='ESP32-S3'||manifest.builds[0].parts?.length!==1||manifest.builds[0].parts[0].offset!==0||manifest.builds[0].parts[0].path!==`TeeNet-${manifest.version}-factory.bin`)throw new Error('Installationsdateien passen nicht zum ESP32-S3.');
    version=manifest.version;
    document.getElementById('install-version').textContent=`TeeNet ${version}`;
    // A versioned manifest prevents a publication during an open page from mixing builds.
    selectBoard();
    let moduleTimer;
    try{await Promise.race([import('https://unpkg.com/esp-web-tools@10/dist/web/install-button.js?module'),new Promise((_,reject)=>{moduleTimer=setTimeout(()=>reject(new Error('USB-Werkzeug nicht erreichbar. Bitte die Seite erneut laden.')),15000);})]);}
    finally{clearTimeout(moduleTimer);}
    if(!customElements.get('esp-web-install-button'))throw new Error('USB-Werkzeug konnte nicht geladen werden.');
    ready=true;installer.hidden=false;sync();status.textContent='Board bestätigen und ESP verbinden.';
  }catch(error){fail(error.name==='AbortError'?'Installationsdateien nicht erreichbar. Bitte später erneut versuchen.':error.message+' Alternativ das USB-Paket verwenden.');}
  finally{clearTimeout(timeout);}
}
setup();

// Observe the public dialog DOM; never operate the serial port or cancel a flash.
let progressTimer=null,progressStarted=0;
function installProgress(){
  const dialog=document.querySelector('ewt-install-dialog');
  if(!dialog){clearInterval(progressTimer);progressTimer=null;progressStarted=0;return;}
  const root=dialog.shadowRoot,progress=root?.querySelector('ewt-page-progress');
  if(!progress){
    root?.querySelector('[data-teenet-progress]')?.remove();
    status.textContent='Nach erfolgreicher Installation mit Wallbox-EMS-Setup verbinden.';
    return;
  }
  if(!progressStarted)progressStarted=Date.now();
  const seconds=Math.floor((Date.now()-progressStarted)/1000),elapsed=`${Math.floor(seconds/60)}:${String(seconds%60).padStart(2,'0')} min`;
  const label=typeof progress.label==='string'?progress.label.toLowerCase():'';
  const message=label.includes('eras')?'Speicher wird gelöscht. Noch keine Prozentanzeige; USB verbunden lassen.':label.includes('writ')?'Firmware wird geschrieben. USB verbunden lassen.':'Installation läuft. USB verbunden lassen.';
  status.textContent=`${message} · ${elapsed}`;
  const host=root.querySelector('ew-dialog');if(!host)return;
  let hint=host.querySelector('[data-teenet-progress]');
  if(!hint){hint=document.createElement('p');hint.dataset.teenetProgress='true';hint.slot='content';hint.style.cssText='font:12px/1.5 system-ui;color:#657786;margin:4px 0 12px';host.append(hint);}
  hint.textContent=`${message} Verstrichen: ${elapsed}${seconds>=180?' · Falls der Dialog einen Fehler meldet, Verbindung und Flashmodus prüfen.':''}`;
}
const dialogs=new MutationObserver(()=>{
  if(!progressTimer&&document.querySelector('ewt-install-dialog')){progressTimer=setInterval(installProgress,1000);installProgress();}
});
dialogs.observe(document.body,{childList:true});
