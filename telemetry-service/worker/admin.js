import {json,headers} from './common.js';
import {authRoute,authPage,authenticated} from './auth.js';
const page=`<!doctype html><html lang="de"><meta charset="utf-8"><meta name="viewport" content="width=device-width,initial-scale=1"><title>TeeNet · Gerätestatistik</title>
<style>:root{font:16px system-ui;color:#173434;background:#eff5f3}body{max-width:1000px;margin:4rem auto;padding:0 1.2rem}header{display:flex;justify-content:space-between;align-items:center}h1{font-size:2rem}.cards{display:grid;grid-template-columns:repeat(auto-fit,minmax(190px,1fr));gap:1rem}.card,section{background:white;border:1px solid #d5e2dd;border-radius:18px;padding:1.5rem}.card b{display:block;font-size:2.8rem;margin:.4rem 0}section{margin:1rem 0}table{border-collapse:collapse;width:100%;text-align:left}td,th{padding:.8rem;border-bottom:1px solid #e4ebe8}p,small{color:#536e66}button,a{color:#09634e}button{border:1px solid #bed5ca;padding:.6rem 1rem;border-radius:9px;background:white}footer{font-size:.85rem;margin:2rem 0}</style>
<header><div><small>TEENET · NUR FÜR DICH</small><h1>Geräte im Betrieb</h1></div><div><button id="refresh">Aktualisieren</button> <button id="logout">Abmelden</button></div></header>
<p id="status" role="status">Statistik wird geladen …</p><div class="cards"><div class="card">Jetzt online<b id="online">—</b><small>Letztes Lebenszeichen vor höchstens 10 Minuten</small></div><div class="card">Letzte 24 Stunden<b id="day">—</b><small>Unterschiedliche Geräte</small></div><div class="card">Letzte 30 Tage<b id="month">—</b><small>Gemeldete Installationen</small></div></div>
<section><h2>Firmwareversionen</h2><table><thead><tr><th>Version</th><th>Online</th><th>24 Stunden</th><th>30 Tage</th></tr></thead><tbody id="versions"></tbody></table></section>
<section><h2>Was wird gezählt?</h2><p>Installationen mit bestätigter Gerätestatistik senden etwa alle fünf Minuten ein Lebenszeichen. Die Kennung ist zufällig. Es werden keine Lade-, WLAN-, Standort- oder Kontodaten gemeldet. Inaktive Einträge werden nach 30 Tagen gelöscht.</p><p>Die Zählung zeigt gemeldete Geräte. Alte Firmware, abgemeldete Geräte und Installationen ohne Internet werden nicht vollständig erfasst. Ein Werksreset erzeugt eine neue Kennung.</p></section><footer>Die Übersicht ist durch deine persönliche Kennwortanmeldung geschützt. <a href="https://stetastic.github.io/TeeNet/">Zur Demo</a></footer>
<script>document.getElementById('logout').onclick=async()=>{const button=document.getElementById('logout');button.disabled=true;try{const response=await fetch('/api/logout',{method:'POST'});if(!response.ok)throw Error('Abmelden fehlgeschlagen. Bitte erneut versuchen.');location.replace('/login');}catch(error){document.getElementById('status').textContent=error.message;button.disabled=false;}};async function refresh(){const status=document.getElementById('status');try{const r=await fetch('/api/summary',{cache:'no-store'});if(!r.ok)throw Error('Anmeldung abgelaufen oder Datenbank nicht verfügbar. Bitte neu anmelden.');const s=await r.json();for(const key of ['online','day','month'])document.getElementById(key).textContent=s.counts[key];const rows=document.getElementById('versions');rows.replaceChildren();for(const row of s.versions){const tr=document.createElement('tr');for(const key of ['version','online','day','month']){const td=document.createElement('td');td.textContent=row[key];tr.append(td);}rows.append(tr);}if(!s.versions.length){const tr=document.createElement('tr'),td=document.createElement('td');td.colSpan=4;td.textContent='Noch keine Geräte gemeldet.';tr.append(td);rows.append(tr);}status.textContent='Stand: '+new Date(s.timestamp*1000).toLocaleString('de-DE');}catch(e){status.textContent=e.message;}}document.getElementById('refresh').onclick=refresh;refresh();setInterval(refresh,60000);</script></html>`;
export default {
 async fetch(request,env,ctx){
  void ctx;
  const path=new URL(request.url).pathname;
  try{
   const routed=await authRoute(request,env);if(routed)return routed;
   if(request.method==='GET'&&(path==='/login'||path==='/setup'))return authPage(path==='/setup');
   if(!await authenticated(request,env))return path==='/api/summary'?json({error:'Anmeldung erforderlich.'},401):
    new Response(null,{status:302,headers:{...headers,Location:'/login'}});
  }catch{return json({error:'Authentication temporarily unavailable'},503);}
  if(request.method!=='GET')return json({error:'Method not allowed'},405);
  if(path==='/')return new Response(page,{headers:{...headers,'Content-Type':'text/html; charset=utf-8','Content-Security-Policy':"default-src 'none'; script-src 'unsafe-inline'; style-src 'unsafe-inline'; connect-src 'self'; frame-ancestors 'none'; base-uri 'none'"}});
  if(path!=='/api/summary')return json({error:'Not found'},404);
  if(!env.DB)return json({error:'Storage unavailable'},503);
  const now=Math.floor(Date.now()/1000);
  try{
   const result=await env.DB.prepare(`SELECT version, SUM(last_seen>=?) AS online,SUM(last_seen>=?) AS day,COUNT(*) AS month
    FROM devices WHERE last_seen>=? GROUP BY version ORDER BY MAX(last_seen) DESC`).bind(now-600,now-86400,now-30*86400).all();
   const versions=result.results||[],counts={online:0,day:0,month:0};for(const row of versions)for(const k of Object.keys(counts))counts[k]+=Number(row[k]);
   return json({timestamp:now,counts,versions});
  }catch{return json({error:'Storage temporarily unavailable'},503);}
 }
};

