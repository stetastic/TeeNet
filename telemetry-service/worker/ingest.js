import {REVISION,json,digest,expire} from './common.js';
const registrations=new Map();
async function registrationAllowed(request){
  // Per-isolate abuse brake only. IPs are never persisted or logged by this application.
  const hour=Math.floor(Date.now()/3600000), key=await digest((request.headers.get('CF-Connecting-IP')||'unknown')+':'+hour);
  if(registrations.size>2000)registrations.clear();
  const count=registrations.get(key)||0;registrations.set(key,count+1);return count<20;
}
export default {
 async fetch(request,env,ctx){
  const path=new URL(request.url).pathname;
  if(path==='/health'&&request.method==='GET')return json({ok:true,service:'TeeNet heartbeat',consent_revision:REVISION});
  if(path!=='/v1/heartbeat')return json({error:'Not found'},404);
  if(!['POST','DELETE'].includes(request.method))return json({error:'Method not allowed'},405);
  if(request.headers.has('Origin'))return json({error:'Device requests only'},403);
  if(!request.headers.get('Content-Type')?.startsWith('application/json'))return json({error:'JSON required'},415);
  if(Number(request.headers.get('Content-Length')||0)>768)return json({error:'Too large'},413);
  let body;try{
   const reader=request.body?.getReader();if(!reader)return json({error:'Body required'},400);
   const parts=[];let size=0;
   for(;;){const {value,done}=await reader.read();if(done)break;size+=value.length;if(size>768){await reader.cancel();return json({error:'Too large'},413);}parts.push(value);}
   const bytes=new Uint8Array(size);let offset=0;for(const part of parts){bytes.set(part,offset);offset+=part.length;}
   body=JSON.parse(new TextDecoder().decode(bytes));
  }catch{return json({error:'Invalid JSON'},400);}
  const secret=request.headers.get('Authorization')?.match(/^Bearer ([a-f0-9]{64})$/)?.[1];
  if(!secret||!body||typeof body!=='object'||!(/^[a-f0-9]{32}$/).test(body.id))return json({error:'Invalid device credentials'},400);
  if(!env.DB)return json({error:'Storage unavailable'},503);
  const id=await digest(body.id),key=await digest(secret),now=Math.floor(Date.now()/1000);
  try{
   const existing=await env.DB.prepare('SELECT key_hash,last_seen FROM devices WHERE id_hash=?').bind(id).first();
   if(existing&&existing.key_hash!==key)return json({error:'Unauthorized'},401);
   if(request.method==='DELETE'){
    if(Object.keys(body).some(k=>k!=='id'))return json({error:'Invalid fields'},400);
    await env.DB.prepare('DELETE FROM devices WHERE id_hash=? AND key_hash=?').bind(id,key).run();
    return json({ok:true,deleted:true});
   }
   if(Object.keys(body).sort().join(',')!=='build,consent_revision,hardware,id,version'||
      !(/^\d{1,3}\.\d{1,4}$/).test(body.version)||!(/^[a-f0-9]{12}$/).test(body.build)||
      !['esp32s3-n16r8','andreas-testboard'].includes(body.hardware)||body.consent_revision!==REVISION)
    return json({error:'Invalid fields or missing current consent'},400);
   if(existing&&existing.last_seen>now-120)return json({ok:true,next_seconds:300});
   if(!existing&&!await registrationAllowed(request))return json({error:'Registration rate limited'},429);
   const saved=await env.DB.prepare(`INSERT INTO devices(id_hash,key_hash,first_seen,last_seen,version,build,hardware,consent_revision)
     VALUES(?,?,?,?,?,?,?,?) ON CONFLICT(id_hash) DO UPDATE SET last_seen=excluded.last_seen,version=excluded.version,
     build=excluded.build,hardware=excluded.hardware,consent_revision=excluded.consent_revision
     WHERE devices.key_hash=excluded.key_hash RETURNING id_hash`).bind(id,key,now,now,body.version,body.build,body.hardware,body.consent_revision).first();
   if(!saved)return json({error:'Unauthorized'},401);
   if(crypto.getRandomValues(new Uint8Array(1))[0]<8)ctx.waitUntil(expire(env).catch(()=>{}));
   return json({ok:true,next_seconds:300});
  }catch{return json({error:'Storage temporarily unavailable'},503);}
 },
 async scheduled(_event,env){await expire(env);}
};
