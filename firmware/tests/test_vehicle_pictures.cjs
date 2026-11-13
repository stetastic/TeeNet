const {test}=require('node:test'),assert=require('node:assert/strict');
const pictures=require('../main/vehicle-pictures.js'),{model}=require('../main/energy.js');
test('realistic vehicle presets are local transparent WebP assets within the firmware budget',()=>{
 assert.deepEqual(pictures.items.map(p=>p.name),['Standard','VW ID.4','VW Tiguan','Mercedes GLB','Cupra Born','Škoda Enyaq']);
 const fs=require('node:fs'),path=require('node:path');let total=0;
 for(const [id,file] of [[1,'id4.webp'],[2,'tiguan.webp'],[3,'glb.webp'],[4,'born.webp'],[5,'enyaq.webp']]){
  const uri=pictures.source(id);assert(uri.startsWith('data:image/webp;base64,'));
  const raw=Buffer.from(uri.split(',')[1],'base64');
  assert.deepEqual(raw,fs.readFileSync(path.join(__dirname,'../design/vehicle-images',file)));
  assert.equal(raw.toString('ascii',0,4),'RIFF');assert.equal(raw.toString('ascii',8,12),'WEBP');
  assert.equal(raw.readUInt32LE(4)+8,raw.length);assert.equal(raw.toString('ascii',12,16),'VP8X');
  assert(raw[20]&0x10,'alpha channel must be retained');
  assert.equal(raw.readUIntLE(24,3)+1,384);assert.equal(raw.readUIntLE(27,3)+1,256);
  assert(raw.length<30000);total+=raw.length;
 }
 assert(total<110000);assert.notEqual(pictures.source(1),pictures.source(2));assert.equal(pictures.source(999),'');
 assert(pictures.source(0).startsWith('data:image/svg+xml;base64,'));
});
test('graphic SOC belongs to the recognized car and never falls back to another vehicle',()=>{
 const s={basic_mode:false,vehicle_soc_enabled:true,car_soc_pct:31,vehicle_profiles_enabled:true,vehicle_profile_active:true,vehicle_profile_soc_pct:64};
 assert.equal(model(s,true).carSoc,64);
 assert.equal(model({...s,vehicle_profile_soc_pct:0},true).carSoc,0);
 assert.equal(model({...s,vehicle_profile_soc_pct:null},true).carSoc,null);
 assert.equal(model({...s,vehicle_profile_active:false},true).carSoc,null);
 assert.equal(model({...s,vehicle_profiles_enabled:false,vehicle_profile_active:false},true).carSoc,31);
 assert.equal(model(s,false).carSoc,null);assert.equal(model({...s,basic_mode:true},true).carSoc,null);
});
