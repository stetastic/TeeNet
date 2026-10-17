export const REVISION='2026-10-16.1';
export const headers={ 'Cache-Control':'no-store', 'X-Content-Type-Options':'nosniff', 'Referrer-Policy':'no-referrer', 'X-Frame-Options':'DENY' };
export const json=(data,status=200)=>new Response(JSON.stringify(data),{status,headers:{...headers,'Content-Type':'application/json; charset=utf-8'}});
export async function digest(text){return Array.from(new Uint8Array(await crypto.subtle.digest('SHA-256',new TextEncoder().encode(text))),b=>b.toString(16).padStart(2,'0')).join('');}
export async function expire(env){await env.DB.prepare('DELETE FROM devices WHERE id_hash IN (SELECT id_hash FROM devices WHERE last_seen < ? LIMIT 500)').bind(Math.floor(Date.now()/1000)-30*86400).run();}
