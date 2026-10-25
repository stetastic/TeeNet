const fs=require('node:fs'),vm=require('node:vm'),assert=require('node:assert/strict');
const source=fs.readFileSync('main/dashboard.js','utf8');
const start=source.indexOf('let statusPromise=null'),end=source.indexOf('\nasync function loadHistory(',start);
assert(start>0&&end>start,'test the actual dashboard status polling functions');
const pollingSource=source.slice(start,end);
const compactPath='/api/status?profile=dashboard',fullPath='/api/status';
const tick=()=>new Promise(resolve=>setImmediate(resolve));

function setup(){
  const nodes=new Map(),requests=[],rendered=[],observed=[],connections=[],messages=[];
  let active=0,maxActive=0,reloads=0;
  function node(id,tagName='DIV'){
    if(!nodes.has(id))nodes.set(id,{
      id,tagName,hidden:false,open:false,parentElement:null,textContent:'',dataset:{},children:[],listeners:new Map(),
      addEventListener(type,callback){this.listeners.set(type,callback);},
      replaceChildren(...children){this.children=children;},
      closest(selector){for(let current=this;current;current=current.parentElement)if(selector==='details'&&current.tagName==='DETAILS')return current;return null;}
    });
    return nodes.get(id);
  }
  const settings=node('settings','SECTION'),parent=node('maintenance','DETAILS'),diagnostics=node('diagnostic-settings','DETAILS');
  settings.hidden=true;parent.open=true;parent.parentElement=settings;diagnostics.parentElement=parent;
  node('diagnostics').parentElement=diagnostics;
  const context=vm.createContext({
    state:null,token:'',online:true,initialized:true,busy:false,uploading:false,
    settingsRebootPending:false,settingsRebootDeferred:true,statusFailures:0,
    document:{hidden:false,querySelectorAll:()=>[parent,diagnostics]},
    window:{
      location:{reload(){reloads++;}},
      TeeNetHealth:{observe(value){observed.push(value);}},
      TeeNetFirmware:{checkBoot(){return null;}}
    },
    $:node,Math,Number,
    diagnostic:(label,value)=>({label,value}),
    updateUnsavedFunctions(){},showConnection(value){connections.push(value);},
    render(){rendered.push(context.state);},toast(value){messages.push(value);},
    api(path,body,timeout){
      active++;maxActive=Math.max(maxActive,active);
      let resolve,reject;
      const result=new Promise((yes,no)=>{resolve=yes;reject=no;});
      requests.push({path,body,timeout,resolve,reject});
      return result.finally(()=>{active--;});
    }
  });
  vm.runInContext(pollingSource,context);
  return {
    context,node,settings,parent,diagnostics,requests,rendered,observed,connections,messages,
    path:()=>context.statusRequestPath(),poll:()=>context.pollStatus(),
    toggle(element=diagnostics){
      const callback=element.listeners.get('toggle');
      assert.equal(typeof callback,'function','diagnostics and their collapsible ancestors refresh the selected profile');
      callback({target:element});
    },
    diagnosticText(){const box=node('diagnostics');return [box.textContent,...box.children.map(child=>[child.textContent,child.label,child.value].filter(Boolean).join(' '))].join(' ');},
    get active(){return active;},get maxActive(){return maxActive;},get reloads(){return reloads;},
    open(){settings.hidden=false;parent.open=true;diagnostics.open=true;},
    reply(index,extra={}){requests[index].resolve({build_id:'test-build',token:'token',reboot_required:false,...extra});}
  };
}

async function expectRequest(h,index,path){
  await tick();
  assert.equal(h.requests.length,index+1,'only the expected serialized requests were started');
  assert.equal(h.requests[index].path,path);
  assert.equal(h.requests[index].body,undefined,'status polling remains read-only');
  assert.equal(h.requests[index].timeout,12000);
  assert.equal(h.maxActive,1,'status requests never overlap');
}

(async()=>{
  {
    const h=setup();
    assert.equal(h.path(),compactPath,'overview uses the compact profile');
    h.diagnostics.open=true;
    assert.equal(h.path(),compactPath,'a retained open diagnostic card on a hidden page does not fetch detail');
    h.settings.hidden=false;
    assert.equal(h.path(),fullPath,'visible diagnostics use the compatible full response');
    h.parent.open=false;
    assert.equal(h.path(),compactPath,'a collapsed ancestor hides diagnostics');
    h.parent.open=true;h.diagnostics.open=false;
    assert.equal(h.path(),compactPath,'ordinary settings polling stays compact');
    h.diagnostics.open=true;h.settings.hidden=true;
    assert.equal(h.path(),compactPath,'returning to overview restores compact polling');
  }
  {
    const h=setup(),first=h.poll(),second=h.poll();
    await expectRequest(h,0,compactPath);
    h.reply(0,{target_current_a:8});await Promise.all([first,second]);
    assert.equal(h.rendered.length,1,'concurrent callers render one shared response');
    assert.equal(h.observed.length,1);
    assert.equal(h.context.state.target_current_a,8);
    assert.equal(h.context.token,'token');
    assert.equal(h.active,0);
    const next=h.poll();await expectRequest(h,1,compactPath);
    h.reply(1,{target_current_a:10});await next;
    assert.equal(h.context.state.target_current_a,10);
  }
  {
    const h=setup();h.open();
    const pending=h.poll();await expectRequest(h,0,fullPath);
    h.reply(0,{wallbox_rx_tail_hex:'AABB',largest_free_block:20000});await pending;
    assert.equal(h.context.state.wallbox_rx_tail_hex,'AABB','diagnostics receive fresh full data');
    h.parent.open=false;
    const compact=h.poll();await expectRequest(h,1,compactPath);
    h.reply(1,{target_current_a:9});await compact;
    assert.equal(h.context.state.wallbox_rx_tail_hex,undefined,'compact samples never inherit old raw detail');
    assert.equal(h.context.state.largest_free_block,undefined,'old diagnostic metrics are not merged into compact data');
  }
  {
    const h=setup(),first=h.poll();await expectRequest(h,0,compactPath);
    h.open();const joined=h.poll();
    assert.equal(h.requests.length,1,'opening diagnostics joins a compact request already in progress');
    h.reply(0,{target_current_a:6});await expectRequest(h,1,fullPath);
    h.reply(1,{wallbox_rx_tail_hex:'CCDD',target_current_a:7});
    await Promise.all([first,joined]);
    assert.equal(h.context.state.wallbox_rx_tail_hex,'CCDD');
    assert.equal(h.context.state.target_current_a,7,'the original caller waits until the newly selected profile finishes');
    assert.equal(h.rendered.at(-1).wallbox_rx_tail_hex,'CCDD');
    assert.equal(h.requests.length,2,'one full follow-up serves every waiting caller');
  }
  {
    const h=setup();h.open();
    const first=h.poll();await expectRequest(h,0,fullPath);
    h.settings.hidden=true;const joined=h.poll();
    h.reply(0,{wallbox_rx_tail_hex:'STALE',target_current_a:6});
    await expectRequest(h,1,compactPath);
    assert.equal(h.rendered.some(value=>value.wallbox_rx_tail_hex==='STALE'),false,'full detail is discarded after leaving diagnostics');
    assert.equal(h.observed.some(value=>value.wallbox_rx_tail_hex==='STALE'),false,'discarded full data does not enter retained display samples');
    h.reply(1,{target_current_a:11});await Promise.all([first,joined]);
    assert.equal(h.context.state.wallbox_rx_tail_hex,undefined);
    assert.equal(h.context.state.target_current_a,11);
  }
  {
    const h=setup(),pending=h.poll();await expectRequest(h,0,compactPath);
    h.open();h.diagnostics.open=false;
    h.reply(0,{target_current_a:12});await pending;
    assert.equal(h.requests.length,1,'a transient profile change needs no follow-up when the final selection matches');
    assert.equal(h.context.state.target_current_a,12);
  }
  {
    const h=setup(),pending=h.poll();await expectRequest(h,0,compactPath);
    h.requests[0].reject(Error('offline'));
    await assert.rejects(pending,/offline/);
    assert.equal(h.rendered.length,0,'a failed response is never rendered');
    assert.equal(h.active,0);
    const recovered=h.poll();await expectRequest(h,1,compactPath);
    h.reply(1,{target_current_a:13});await recovered;
    assert.equal(h.context.state.target_current_a,13,'a failed request clears the shared promise for recovery');
    assert.equal(h.context.statusFailures,0);
    assert.equal(h.connections.at(-1),true);
  }
  {
    const h=setup(),initial=h.poll();await expectRequest(h,0,compactPath);
    h.reply(0,{target_current_a:14});await initial;
    h.settings.hidden=false;h.diagnostics.open=true;h.parent.open=false;
    h.toggle();await tick();
    assert.equal(h.requests.length,1,'a diagnostic toggle inside a closed ancestor does not fetch full status');
    h.parent.open=true;h.toggle(h.parent);
    await expectRequest(h,1,fullPath);
    assert.match(h.diagnosticText(),/geladen|Laden|lädt/i,'opening diagnostics clears the old display while detail loads');
    h.reply(1,{wallbox_rx_tail_hex:'EEFF'});await tick();
    assert.equal(h.context.state.wallbox_rx_tail_hex,'EEFF','opening an ancestor requests current diagnostics immediately');
    h.toggle();await tick();
    assert.equal(h.requests.length,2,'unchanged open diagnostics do not repeat an accepted full response');
  }
  {
    const h=setup(),initial=h.poll();await expectRequest(h,0,compactPath);
    h.reply(0,{target_current_a:15});await initial;
    h.open();h.toggle();await expectRequest(h,1,fullPath);
    h.requests[1].reject(Error('diagnostic request failed'));await tick();
    assert.match(h.diagnosticText(),/Diagnose.*nicht erreichbar/i,'an on-demand failure is reported in the diagnostic card');
    assert.equal(h.context.state.target_current_a,15,'failed detail does not replace accepted overview data');
    assert.equal(h.context.statusFailures,0,'on-demand detail failure does not count as an overview connection failure');
    assert(h.connections.every(Boolean),'on-demand detail failure leaves the accepted connection state intact');
    assert.equal(h.messages.length,0,'local diagnostic errors do not raise a general toast');
    h.toggle();await expectRequest(h,2,fullPath);
    h.reply(2,{wallbox_rx_tail_hex:'RECOVERED'});await tick();
    assert.equal(h.context.state.wallbox_rx_tail_hex,'RECOVERED','diagnostics recover on the next request');
  }
  {
    const h=setup();h.open();
    h.context.initialized=false;h.toggle();await tick();
    assert.equal(h.requests.length,0,'a toggle does not bypass initial dashboard loading');
    h.context.initialized=true;h.context.document.hidden=true;h.toggle();await tick();
    assert.equal(h.requests.length,0,'a background tab does not request diagnostic data');
    h.context.document.hidden=false;h.context.online=false;h.toggle();await tick();
    assert.equal(h.requests.length,0,'on-demand diagnostics respect the offline state');
    h.context.online=true;h.context.busy=true;h.toggle();await tick();
    assert.equal(h.requests.length,0,'on-demand diagnostics do not interrupt an active operation');
    h.context.busy=false;h.context.uploading=true;h.toggle();await tick();
    assert.equal(h.requests.length,0,'on-demand diagnostics respect an upload in progress');
    h.context.uploading=false;h.toggle();await expectRequest(h,0,fullPath);
    h.reply(0,{wallbox_rx_tail_hex:'READY'});await tick();
    assert.equal(h.context.state.wallbox_rx_tail_hex,'READY');
  }
  console.log('PASS: compact dashboard polling, visible diagnostics, serialized profile changes, discarded stale detail and error recovery');
})().catch(error=>{console.error(error);process.exitCode=1;});
