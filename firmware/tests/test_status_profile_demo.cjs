const fs=require('node:fs'),os=require('node:os'),path=require('node:path'),vm=require('node:vm');
const {spawnSync}=require('node:child_process'),assert=require('node:assert/strict');
const root=path.resolve(__dirname,'..'),temporary=fs.mkdtempSync(path.join(os.tmpdir(),'teenet-status-profile-'));
const html='<html><head><script defer src="/demo-api.js"></script><script defer src="/evcc-demo-api.js"></script><script defer src="/expert-demo-api.js"></script><script defer src="/dashboard.js?v=test"></script></head></html>';
const candidates=[process.env.TEENET_PYTHON,path.join(os.homedir(),'.cache/codex-runtimes/codex-primary-runtime/dependencies/python/python.exe'),'python3','python'].filter(Boolean);
let python;
for(const candidate of candidates){const result=spawnSync(candidate,['--version'],{encoding:'utf8'});if(result.status===0){python=candidate;break;}}
assert(python,'Python is required to test the production demo profile generator');
function generate(directory,source){
  fs.mkdirSync(directory,{recursive:true});
  if(!fs.existsSync(path.join(directory,'index.html')))fs.writeFileSync(path.join(directory,'index.html'),html);
  const command=[path.join(root,'tools/status_profile_demo.py'),directory];
  if(source)command.push('--source',source);
  const result=spawnSync(python,command,{cwd:root,encoding:'utf8'});
  assert.equal(result.status,0,result.stderr||result.error?.message);
  return JSON.parse(result.stdout);
}
function setup(script,fetch){
  const location={origin:'https://demo.local',href:'https://demo.local/#overview'};
  const window={location,fetch};
  vm.runInNewContext(script,{window,URL,Response,Headers,String,Array,JSON});
  return window;
}

(async()=>{
  try{
    const fields=generate(temporary),shim=fs.readFileSync(path.join(temporary,'status-profile-demo.js'),'utf8');
    assert(fields.includes('wallbox_rx_tail_hex')&&fields.includes('largest_free_block'));
    assert(fields.includes('last_sent_a')&&fields.includes('house_a'),'owned array fields follow the same diagnostic guards as scalar fields');
    assert(!fields.includes('last_sent_age_ms')&&!fields.includes('measurement_age_s')&&!fields.includes('token'));
    const installed=fs.readFileSync(path.join(temporary,'index.html'),'utf8');
    assert(installed.indexOf('/expert-demo-api.js')<installed.indexOf('/status-profile-demo.js'));
    assert(installed.indexOf('/status-profile-demo.js')<installed.indexOf('/dashboard.js'));
    assert(installed.includes('/status-profile-demo.js?v=test'),'the generated shim uses the dashboard cache version');
    assert.deepEqual(generate(temporary),fields);
    assert.equal(fs.readFileSync(path.join(temporary,'index.html'),'utf8'),installed,'installation is idempotent');

    // Exercise balanced parsing with misleading C strings, comments, character
    // literals and nested braces, while preserving the actual firmware source.
    const source=fs.readFileSync(path.join(root,'main/app.c'),'utf8');
    const marker=/\bstatus_json_profile\s*\([^)]*\)\s*\{/;
    assert(marker.test(source));
    const extra=String.raw`
      const char *fake="if(include_diagnostics){cJSON_AddNumberToObject(o,\"__string_fake\",1);}";
      /* } if(include_diagnostics){ cJSON_AddNumberToObject(o,"__comment_fake",1); } */
      cJSON_AddNumberToObject(o,"__shared",1);
      if(include_diagnostics){
        char brace='}'; const char *text="}";
        if(something){cJSON_AddNumberToObject(o,"__parser_real",1);}
        json_add_owned_to_object(o,"__owned_real",nested);
        nullable(nested,"__nested_only",1,true);
        cJSON_AddNumberToObject(o,"__shared",2);
        // } cJSON_AddNumberToObject(o,"__line_fake",1);
      }
    `;
    const fixture=path.join(temporary,'parser.c');fs.writeFileSync(fixture,source.replace(marker,match=>match+extra));
    const parsed=generate(path.join(temporary,'parser'),fixture);
    assert.deepEqual(parsed,[...fields,'__parser_real','__owned_real'].sort(),'only real guarded root additions become exclusions, including owned children');

    const detail=Object.fromEntries(fields.map(name=>[name,`detail:${name}`]));
    const shared={token:'csrf-fixture',version:'1.22',build_id:'demo-build',enabled:false,target_current_a:0,last_sent_age_ms:50,
      firmware_check_pending:false,app_elf_sha256:'fixture-hash',actual_a:[0,0,0],meter_a:[0,0,0],
      measurement_age_s:{wallbox:1,house:2,battery:3,pv:4,wallbox_rx_tail_hex:'nested preserved'},
      gpio_pins:{wallbox_tx_pin:17},charge_plan:{active:false,phase:'off'},energy:[{source:'current_estimate',total_kwh:2}]};
    const full={...shared,...detail},calls=[];
    // The inner shims can add every diagnostic key after an earlier shim has
    // already returned a compact body. The generated outer wrapper filters last.
    const inner=async(input,options)=>{
      calls.push({input,options});
      const base=new Response(JSON.stringify(shared),{headers:{'Content-Type':'application/json'}});
      const value=await base.json();Object.assign(value,detail);
      return new Response(JSON.stringify(value),{status:201,statusText:'Synthetic status',headers:{'Content-Type':'application/json','Cache-Control':'no-store','X-Demo':'preserved','Content-Length':'99999','Content-Encoding':'gzip'}});
    };
    const window=setup(shim,inner);
    const legacy=await window.fetch('/api/status');
    assert.equal(legacy.status,201);assert.deepEqual(await legacy.json(),full,'legacy status remains complete');
    const compact=await window.fetch('/api/status?profile=dashboard');
    assert.equal(compact.status,201);assert.equal(compact.statusText,'Synthetic status');
    assert.equal(compact.headers.get('X-Demo'),'preserved');assert.equal(compact.headers.get('Cache-Control'),'no-store');
    assert.equal(compact.headers.get('Content-Type'),'application/json');
    assert.equal(compact.headers.get('Content-Length'),null);assert.equal(compact.headers.get('Content-Encoding'),null);
    assert.deepEqual(await compact.json(),{...shared,status_profile:'dashboard'},'the compact response matches the C-derived root exclusion list exactly');
    const options={headers:{'X-Test':'forwarded'}};
    const requested=await window.fetch(new Request('https://demo.local/api/status?x=1&profile=dashboard'),options);
    assert.deepEqual(await requested.json(),{...shared,status_profile:'dashboard'});
    assert.equal(calls.at(-1).options,options,'request options are forwarded intact');
    const urlResponse=await window.fetch(new URL('https://demo.local/api/status?profile=dashboard'));
    assert.deepEqual(await urlResponse.json(),{...shared,status_profile:'dashboard'},'URL inputs select the same profile');

    for(const [input,options] of [
      ['/api/status',undefined],['/api/status?profile=full',undefined],['/api/status?profile=Dashboard',undefined],
      ['/api/other?profile=dashboard',undefined],['https://other.local/api/status?profile=dashboard',undefined],
      ['/api/status?profile=dashboard',{method:'POST'}],['/api/status?profile=dashboard',{method:'HEAD'}],
      [new Request('https://demo.local/api/status?profile=dashboard',{method:'POST'}),undefined]
    ]){
      const original=new Response(JSON.stringify(full),{headers:{'X-Unchanged':'yes'}});
      const pass=setup(shim,async()=>original),response=await pass.fetch(input,options);
      assert.equal(response,original,'nonmatching requests pass through the original response');
      assert.equal(response.bodyUsed,false);assert.deepEqual(await response.json(),full);
    }
    for(const original of [
      new Response('not available',{status:503,statusText:'Unavailable',headers:{'X-Error':'preserved'}}),
      new Response('not JSON',{status:200}),new Response('null'),new Response('[1,2,3]'),new Response(null,{status:204})
    ]){
      const pass=setup(shim,async()=>original),response=await pass.fetch('/api/status?profile=dashboard');
      assert.equal(response,original,'errors and non-status bodies are untouched');assert.equal(response.bodyUsed,false);
    }
    const failure=Error('synthetic network failure'),offline=setup(shim,async()=>{throw failure;});
    await assert.rejects(offline.fetch('/api/status?profile=dashboard'),error=>error===failure,'fetch failures keep their original error');
    console.log(`PASS: C-derived demo status profile (${fields.length} diagnostic fields), outer shim ordering, preserved control data and response passthrough`);
  }finally{fs.rmSync(temporary,{recursive:true,force:true});}
})().catch(error=>{console.error(error);process.exitCode=1;});
