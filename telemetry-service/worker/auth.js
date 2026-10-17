import {digest,json,headers} from './common.js';
const COOKIE='__Host-teenet_session',TTL=43200;
const hex=bytes=>Array.from(bytes,b=>b.toString(16).padStart(2,'0')).join('');
export const random=()=>hex(crypto.getRandomValues(new Uint8Array(32)));
function equal(a,b){if(typeof a!=='string'||typeof b!=='string'||a.length!==b.length)return false;let d=0;for(let i=0;i<a.length;i++)d|=a.charCodeAt(i)^b.charCodeAt(i);return d===0;}
async function passwordHash(password,salt){
 const key=await crypto.subtle.importKey('raw',new TextEncoder().encode(password),'PBKDF2',false,['deriveBits']);
 return hex(new Uint8Array(await crypto.subtle.deriveBits({name:'PBKDF2',hash:'SHA-256',salt:new TextEncoder().encode(salt),iterations:100000},key,256)));
}
function token(request){return request.headers.get('Cookie')?.split(';').map(s=>s.trim()).find(s=>s.startsWith(COOKIE+'='))?.slice(COOKIE.length+1)||'';}
export async function authenticated(request,env){
 const t=token(request);if(!/^[a-f0-9]{64}$/.test(t)||!env.DB)return false;
 return !!await env.DB.prepare('SELECT token_hash FROM admin_sessions WHERE token_hash=? AND expires>?').bind(await digest(t),Math.floor(Date.now()/1000)).first();
}
export function authPage(setup=false){
 const title=setup?'Deinen Statistikzugang einrichten':'TeeNet · Private Statistik';
 const script=setup?`const token=location.hash.slice(1);history.replaceState(null,'','/setup');document.querySelector('form').onsubmit=async e=>{e.preventDefault();const p=document.getElementById('password').value;if(p!==document.getElementById('repeat').value){document.getElementById('message').textContent='Die Kennwörter stimmen nicht überein.';return;}try{const r=await fetch('/api/setup',{method:'POST',headers:{'Content-Type':'application/json','X-Setup-Token':token},body:JSON.stringify({password:p})});if(!r.ok)throw Error((await r.json()).error);location.href='/login';}catch(e){document.getElementById('message').textContent=e.message;}};`:
 `document.querySelector('form').onsubmit=async e=>{e.preventDefault();try{const r=await fetch('/api/login',{method:'POST',headers:{'Content-Type':'application/json'},body:JSON.stringify({password:document.getElementById('password').value})});if(!r.ok)throw Error((await r.json()).error);location.href='/';}catch(e){document.getElementById('message').textContent=e.message;}};`;
 return new Response(`<!doctype html><html lang="de"><meta charset="utf-8"><meta name="viewport" content="width=device-width,initial-scale=1"><title>${title}</title><style>:root{font:16px system-ui;background:#eff5f3;color:#173434}main{max-width:420px;margin:12vh auto;padding:2rem;background:white;border:1px solid #d5e2dd;border-radius:18px}label{display:block;margin:1.3rem 0}input{box-sizing:border-box;display:block;width:100%;padding:.8rem;margin-top:.4rem;font:inherit}button{background:#09634e;color:white;padding:.8rem 1rem;border:0;border-radius:8px;font:inherit}p{line-height:1.5}#message{color:#a32c27}</style><main><small>TEENET · NUR FÜR DICH</small><h1>${title}</h1><p>${setup?'Lege ein eigenes Kennwort mit mindestens 12 Zeichen fest. Dieser Einrichtungslink funktioniert nur einmal.':'Melde dich mit deinem selbst festgelegten Statistik-Kennwort an.'}</p><form><label>Kennwort<input id="password" type="password" autocomplete="${setup?'new-password':'current-password'}" minlength="12" maxlength="128" required></label>${setup?'<label>Kennwort wiederholen<input id="repeat" type="password" autocomplete="new-password" minlength="12" maxlength="128" required></label>':''}<p id="message" role="alert"></p><button>${setup?'Zugang einrichten':'Anmelden'}</button></form></main><script>${script}</script></html>`,{headers:{...headers,'Content-Type':'text/html; charset=utf-8','Content-Security-Policy':"default-src 'none'; script-src 'unsafe-inline'; style-src 'unsafe-inline'; connect-src 'self'; form-action 'self'; frame-ancestors 'none'; base-uri 'none'"}});
}
export async function authRoute(request,env){
 const path=new URL(request.url).pathname;
 if(!['/api/setup','/api/login','/api/logout'].includes(path))return null;
 if(request.method!=='POST')return json({error:'Method not allowed'},405);
 if(request.headers.get('Origin')!==new URL(request.url).origin)return json({error:'Origin rejected'},403);
 if(!env.DB)return json({error:'Storage unavailable'},503);
 if(path==='/api/logout'){
  const t=token(request);if(t)await env.DB.prepare('DELETE FROM admin_sessions WHERE token_hash=?').bind(await digest(t)).run();
  return new Response('{}',{headers:{...headers,'Content-Type':'application/json','Set-Cookie':`${COOKIE}=; Path=/; Secure; HttpOnly; SameSite=Strict; Max-Age=0`}});
 }
 const reader=request.body?.getReader();if(!reader)return json({error:'Body required'},400);
 let raw='',size=0;const decoder=new TextDecoder();
 for(;;){const {value,done}=await reader.read();if(done)break;size+=value.length;if(size>1024){await reader.cancel();return json({error:'Too large'},413);}raw+=decoder.decode(value,{stream:true});}raw+=decoder.decode();
 let body;try{body=JSON.parse(raw);}catch{return json({error:'Ungültige Eingabe.'},400);}
 if(typeof body.password!=='string'||body.password.length<12||body.password.length>128)return json({error:'Kennwort muss 12 bis 128 Zeichen lang sein.'},400);
 const credentials=await env.DB.prepare('SELECT salt,password_hash FROM admin_credentials WHERE slot=1').first();
 if(path==='/api/setup'){
  const given=request.headers.get('X-Setup-Token')||'';
  if(credentials||!env.SETUP_TOKEN||!equal(await digest(given),await digest(env.SETUP_TOKEN)))return json({error:'Einrichtungslink ungültig oder bereits verwendet.'},403);
  const salt=random(),hash=await passwordHash(body.password,salt);
  const result=await env.DB.prepare('INSERT OR IGNORE INTO admin_credentials(slot,salt,password_hash) VALUES(1,?,?)').bind(salt,hash).run();
  return result.meta?.changes===1?json({ok:true}):json({error:'Zugang bereits eingerichtet.'},409);
 }
 const now=Math.floor(Date.now()/1000);
 const attempt=await env.DB.prepare(`INSERT INTO admin_attempts(slot,started,attempts) VALUES(1,?,1)
  ON CONFLICT(slot) DO UPDATE SET started=CASE WHEN started<? THEN excluded.started ELSE started END,
  attempts=CASE WHEN started<? THEN 1 ELSE attempts+1 END RETURNING attempts`).bind(now,now-900,now-900).first();
 if(attempt.attempts>10)return json({error:'Zu viele Versuche. Bitte in 15 Minuten erneut versuchen.'},429);
 if(!credentials||!equal(await passwordHash(body.password,credentials.salt),credentials.password_hash))return json({error:'Kennwort nicht korrekt.'},401);
 const t=random();await env.DB.batch([
  env.DB.prepare('DELETE FROM admin_attempts WHERE slot=1'),
  env.DB.prepare('DELETE FROM admin_sessions WHERE expires<?').bind(now),
  env.DB.prepare('INSERT INTO admin_sessions(token_hash,expires) VALUES(?,?)').bind(await digest(t),now+TTL)
 ]);
 return new Response('{"ok":true}',{headers:{...headers,'Content-Type':'application/json','Set-Cookie':`${COOKIE}=${t}; Path=/; Secure; HttpOnly; SameSite=Strict; Max-Age=${TTL}`}});
}
