const {chromium}=require('playwright');
const assert=require('node:assert/strict');
const fs=require('node:fs');
const path=require('node:path');
const origin='http://demo.test';
const preview=path.resolve(__dirname,'../preview');

(async()=>{
  const browser=await chromium.launch({headless:true,executablePath:process.env.CHROME_PATH||undefined});
  try{
    const context=await browser.newContext({viewport:{width:1190,height:820},reducedMotion:'reduce'});
    await context.route('**/*',async route=>{
      const url=new URL(route.request().url());
      if(url.origin!==origin)return route.abort();
      const file=path.resolve(preview,'.'+(url.pathname==='/'?'/index.html':url.pathname));
      if(!file.startsWith(preview+path.sep)||!fs.existsSync(file))return route.fulfill({status:404});
      const types={html:'text/html',js:'application/javascript',css:'text/css',svg:'image/svg+xml',json:'application/json',png:'image/png'};
      return route.fulfill({body:fs.readFileSync(file),contentType:types[path.extname(file).slice(1)]||'application/octet-stream'});
    });
    await context.addInitScript(()=>{
      localStorage.setItem('teenet-demo-terms','2026-10-09.2');
      localStorage.setItem('teenet-demo-device-statistics','false');
      sessionStorage.setItem('teenet-demo-welcome-v1','1');
    });
    const p=await context.newPage(),errors=[];
    p.on('pageerror',e=>errors.push(e.message));
    async function ready(){await p.waitForFunction(()=>window.TeeNetEnergy&&typeof initialized!=='undefined'&&initialized&&online);}
    async function status(online=true,heatpump_w=1000){
      await p.evaluate(([connected,w])=>TeeNetEnergy.update({...state,basic_mode:false,zero_feed_enabled:true,
        pv_display_enabled:true,pv_generation_w:2300,heatpump_enabled:true,heatpump_ok:true,heatpump_w:w},connected),[online,heatpump_w]);
    }
    async function style(){
      return p.evaluate(()=>{
        const root=document.querySelector('#teenet-house-mood');
        const line=getComputedStyle(root.querySelector('[data-route="solar"] .en-motion'));
        const fan=getComputedStyle(root.querySelector('.en-heatpump-fan'));
        return {display:line.display,name:line.animationName,state:line.animationPlayState,offset:line.strokeDashoffset,
          fan:fan.animationName,fanState:fan.animationPlayState,fanTransform:fan.transform,override:root.dataset.energyMotion||null};
      });
    }
    async function moving(){
      await status();
      const a=await style();await p.waitForTimeout(230);const b=await style();
      assert.equal(a.name,'en-travel');assert.equal(a.state,'running');assert.notEqual(a.display,'none');
      assert.notEqual(a.offset,b.offset);assert.equal(a.fan,'en-heatpump-fan');assert.equal(a.fanState,'running');
      assert.notEqual(a.fanTransform,b.fanTransform);
    }
    await p.goto(origin+'/#overview');await ready();await status();
    assert.equal((await style()).display,'none');assert.equal((await style()).fan,'none');
    await p.emulateMedia({reducedMotion:'no-preference'});await moving();
    await p.emulateMedia({reducedMotion:'reduce'});assert.equal((await style()).display,'none');
    await p.goto(origin+'/#energiefluss-animation-an');
    await p.waitForURL(origin+'/#overview');await moving();
    assert.equal(await p.evaluate(()=>localStorage.getItem('teenet-energy-motion')),'on');
    await p.reload();await ready();await moving();
    await p.evaluate(()=>{Object.defineProperty(document,'hidden',{configurable:true,value:true});document.dispatchEvent(new Event('visibilitychange'));});
    const hidden=await style();assert.equal(hidden.state,'paused');assert.equal(hidden.fanState,'paused');
    await p.evaluate(()=>{delete document.hidden;document.dispatchEvent(new Event('visibilitychange'));});
    await moving();
    await status(false);
    assert.equal((await style()).state,'paused');await moving();
    await status(true,0);assert.equal((await style()).fan,'none');
    await p.goto(origin+'/#appearance-settings');
    await p.evaluate(()=>{
      window.motionPosts=[];const prev=window.fetch;
      window.fetch=(url,opts)=>{if(opts?.method==='POST')window.motionPosts.push(String(url));return prev(url,opts);};
    });
    await p.locator('#motion-system').click();
    assert.equal(await p.locator('#motion-system').getAttribute('aria-pressed'),'true');
    assert.equal((await style()).display,'none');
    assert.equal(await p.evaluate(()=>localStorage.getItem('teenet-energy-motion')),null);
    await p.locator('#motion-on').click();assert.equal((await style()).override,'on');
    assert.deepEqual(await p.evaluate(()=>window.motionPosts),[]);
    await p.locator('#motion-system').click();
    const second=await context.newPage();await second.goto(origin+'/#energiefluss-animation-an');
    await second.waitForURL(origin+'/#overview');
    await p.waitForFunction(()=>document.querySelector('#teenet-house-mood').dataset.energyMotion==='on');
    assert.deepEqual(errors,[]);
    console.log('PASS: system motion, reduced motion, browser override, live and launch links, reload, cross-tab sync, online/background pauses, idle fan and no settings POST.');
  } finally {await browser.close();}
})().catch(e=>{console.error(e);process.exitCode=1;});
