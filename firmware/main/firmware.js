'use strict';
window.TeeNetFirmware=(()=>{
  function prefixInfo(bytes){
    if(bytes.length<288||bytes[0]!==0xe9||bytes[1]<1||bytes[1]>16||bytes[12]!==9||bytes[13]!==0||bytes[32]!==0x32||bytes[33]!==0x54||bytes[34]!==0xcd||bytes[35]!==0xab)throw Error('TeeNet-OTA-Datei für ESP32-S3 auswählen, kein USB-Image.');
    const text=(offset)=>{let end=offset;while(end<offset+32&&bytes[end])end++;if(end===offset+32)throw Error('Firmware-Kopf ungültig.');return new TextDecoder().decode(bytes.slice(offset,end));};
    const version=text(48);if(text(80)!=='wallbox_ems'||!/^\d+\.\d+$/.test(version))throw Error('Die Datei gehört nicht zu TeeNet.');
    const appHash=Array.from(bytes.slice(176,208),v=>v.toString(16).padStart(2,'0')).join('');
    return {version,appHash,project:'wallbox_ems',chip:'ESP32-S3'};
  }
  async function inspect(file,maximum=2097152){
    if(!file||!file.name.toLowerCase().endsWith('.bin')||file.size<1024||file.size>maximum)throw Error('TeeNet-OTA-Datei (.bin) bis 2 MB auswählen.');
    const bytes=new Uint8Array(await file.arrayBuffer()),info=prefixInfo(bytes);
    let integrity=false;
    if(bytes[23]===1&&window.crypto?.subtle){
      const digest=new Uint8Array(await window.crypto.subtle.digest('SHA-256',bytes.slice(0,-32)));
      if(!digest.every((v,i)=>v===bytes[bytes.length-32+i]))throw Error('Die Firmware ist beschädigt. Bitte erneut herunterladen.');
      integrity=true;
    }
    return {...info,integrity,bytes:file.size};
  }
  function compare(a,b){const [am,an]=a.split('.').map(Number),[bm,bn]=b.split('.').map(Number);return am-bm||an-bn;}
  function pending(){try{return JSON.parse(sessionStorage.getItem('teenet-update')||'null');}catch(_){return null;}}
  function remember(info,build){try{sessionStorage.setItem('teenet-update',JSON.stringify({version:info.version,appHash:info.appHash,build,at:Date.now()}));}catch(_){}}
  function checkBoot(s){
    const p=pending();if(!p)return null;
    if(s.firmware_check_failed){try{sessionStorage.removeItem('teenet-update');}catch(_){}return 'Startprüfung fehlgeschlagen. Ladung bleibt gesperrt; Diagnose prüfen.';}
    if(!s.firmware_check_pending&&s.firmware_check_ok!==false&&s.version===p.version&&(p.appHash&&s.app_elf_sha256?s.app_elf_sha256===p.appHash:s.build_id!==p.build)){try{sessionStorage.removeItem('teenet-update');}catch(_){}return `TeeNet ${s.version} ist installiert und wieder erreichbar. Ladung bleibt ausgeschaltet.`;}
    if(Date.now()-p.at>180000){try{sessionStorage.removeItem('teenet-update');}catch(_){}return 'Update noch nicht bestätigt. Version und Verbindung prüfen.';}
    return null;
  }
  async function backupZip(blob,state){
    const table=Uint32Array.from({length:256},(_,value)=>{for(let i=0;i<8;i++)value=value&1?0xedb88320^(value>>>1):value>>>1;return value>>>0;});
    const crc32=data=>{let crc=0xffffffff;for(const byte of data)crc=table[(crc^byte)&255]^(crc>>>8);return (crc^0xffffffff)>>>0;};
    const text=new TextEncoder(),image=new Uint8Array(await blob.arrayBuffer());
    const entries=[['TeeNet-fullflash.bin',image],['Sicherung.json',text.encode(JSON.stringify({format:'TeeNet-system',schema:1,version:state.version,build_id:state.build_id,bytes:image.length,created:new Date().toISOString(),contains_passwords:true},null,2))],['Wiederherstellen.txt',text.encode('TeeNet – vollständige Systemsicherung\n\nEnthält Firmware, Einstellungen, WLAN-/MQTT-Passwörter und Verlauf. Privat aufbewahren.\n\nWiederherstellung ausschließlich per USB auf demselben ESP32-S3 mit gleicher Flashgröße. Dabei wird der komplette Stand zum Zeitpunkt der Sicherung wiederhergestellt. Ladung zuerst stoppen.\n\nZIP entpacken und mit esptool das vollständige Image ab Adresse 0 schreiben:\npython -m esptool --chip esp32s3 --port COMx write_flash 0 TeeNet-fullflash.bin\n\nCOMx durch den USB-Port ersetzen. Anschließend neu starten, Messwerte und Freigabe prüfen. Diese Datei NICHT als OTA-Update verwenden.\n')]];
    const local=[],central=[];let offset=0,centralSize=0;
    for(const [filename,data] of entries){
      const name=text.encode(filename),crc=crc32(data),header=new Uint8Array(30+name.length),h=new DataView(header.buffer);
      h.setUint32(0,0x04034b50,true);h.setUint16(4,20,true);h.setUint16(6,0x800,true);h.setUint16(12,33,true);h.setUint32(14,crc,true);h.setUint32(18,data.length,true);h.setUint32(22,data.length,true);h.setUint16(26,name.length,true);header.set(name,30);
      const directory=new Uint8Array(46+name.length),d=new DataView(directory.buffer);
      d.setUint32(0,0x02014b50,true);d.setUint16(4,20,true);d.setUint16(6,20,true);d.setUint16(8,0x800,true);d.setUint16(14,33,true);d.setUint32(16,crc,true);d.setUint32(20,data.length,true);d.setUint32(24,data.length,true);d.setUint16(28,name.length,true);d.setUint32(42,offset,true);directory.set(name,46);
      local.push(header,data);central.push(directory);centralSize+=directory.length;offset+=header.length+data.length;
    }
    const end=new Uint8Array(22),e=new DataView(end.buffer);e.setUint32(0,0x06054b50,true);e.setUint16(8,entries.length,true);e.setUint16(10,entries.length,true);e.setUint32(12,centralSize,true);e.setUint32(16,offset,true);
    return new Blob([...local,...central,end],{type:'application/zip'});
  }
  return {prefixInfo,inspect,compare,remember,checkBoot,backupZip};
})();
