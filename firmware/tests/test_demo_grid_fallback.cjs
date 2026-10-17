const fs=require('node:fs'),vm=require('node:vm'),assert=require('node:assert/strict');
const source=fs.readFileSync('../teennet-demo/dist/demo-api.js','utf8');
function demo(fault=''){
  const location={origin:'http://demo.local',search:fault?`?house-phases=${fault}`:''};
  const window={location,fetch(){throw Error('Demo must not access real devices');}};
  vm.runInNewContext(source,{window,location,URL,URLSearchParams,Response,Date,Math,Number,String,Map,JSON,document:{addEventListener(){}}});
  return async(path,body)=>{
    const response=await window.fetch('/api/'+path,body===undefined?{}:{method:'POST',body:JSON.stringify(body)});
    assert.equal(response.status,200);return response.json();
  };
}
const close=(a,b)=>assert(Math.abs(a-b)<.001,`${a} != ${b}`);
(async()=>{
  for(const fault of ['missing','stale']){
    const api=demo(fault);
    await api('config',{basic_mode:false,expert_mode:true,huawei_enabled:true,house_meter_type:'huawei',grid_guard_enabled:true});
    await api('reboot',{});
    await api('control',{mode:'manual',enabled:true,current_a:16});
    let s=await api('status');
    assert.equal(s.grid_guard_fallback,true);assert.equal(s.house_current_ok,false);
    assert.equal(s.grid_guard_limit_kw,8);assert.equal(s.block_reason,'grid_fallback');
    assert.deepEqual(s.house_a,[null,null,null]);close(s.target_current_a*690,8000);
    assert(s.estimate_w<=8000&&s.estimate_w>0);
    await api('control',{current_a:6000/690});s=await api('status');close(s.target_current_a*690,6000);
    await api('control',{enabled:false});s=await api('status');
    close(s.target_current_a,0);close(s.estimate_w,0);assert.equal(s.block_reason,'off');
    await api('config',{grid_guard_enabled:false});
    await api('control',{enabled:true,current_a:16});s=await api('status');
    close(s.target_current_a,16);assert.equal(s.grid_guard_fallback,false);
  }
  const api=demo();
  await api('config',{basic_mode:false,expert_mode:true,huawei_enabled:true,house_meter_type:'huawei',grid_guard_enabled:true});
  await api('reboot',{});
  await api('control',{enabled:true,mode:'manual',current_a:16});
  const s=await api('status');assert.equal(s.grid_guard_ok,true);
  assert.equal(s.grid_guard_fallback,false);close(s.target_current_a,16);
  console.log('Demo: missing/stale phases cap at 8 kW; lower targets, Stop and valid measurements passed');
})().catch(error=>{console.error(error);process.exitCode=1;});
