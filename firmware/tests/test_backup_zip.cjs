'use strict';
const fs=require('node:fs'),vm=require('node:vm'),assert=require('node:assert/strict');
const {spawnSync}=require('node:child_process');
global.window={};global.sessionStorage={getItem:()=>null,setItem:()=>{},removeItem:()=>{}};
vm.runInThisContext(fs.readFileSync('main/firmware.js','utf8'));
(async()=>{
  const bytes=Uint8Array.from({length:16384},(_,i)=>(i*37)%256);
  const zip=await window.TeeNetFirmware.backupZip(new Blob([bytes]),{version:'1.13',build_id:'test'});
  const output='build/test-backup.zip';fs.writeFileSync(output,Buffer.from(await zip.arrayBuffer()));
  const code="import zipfile,json; z=zipfile.ZipFile('build/test-backup.zip'); assert z.testzip() is None; assert z.read('TeeNet-fullflash.bin')==bytes((i*37)%256 for i in range(16384)); s=json.loads(z.read('Sicherung.json')); assert s['bytes']==16384 and s['contains_passwords'] and s['version']=='1.13'; assert b'write_flash 0' in z.read('Wiederherstellen.txt'); print('PASS: full-system ZIP integrity, flash bytes, metadata and USB restoration instructions')";
  const result=spawnSync(process.env.TEENET_TEST_PYTHON||'python',['-c',code],{encoding:'utf8'});assert.equal(result.status,0,result.stderr||result.error?.message);process.stdout.write(result.stdout);
})().catch(error=>{console.error(error);process.exitCode=1;});
