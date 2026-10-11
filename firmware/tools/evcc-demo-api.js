/* Private UI preview. Never connects to the real ESP, Shell or evcc. */
(() => {
  const previous=window.fetch.bind(window);
  const cfg={enabled:false,shell_host:'192.168.1.65',key:'demo-only',phases:3,min_current_a:8.7,max_current_a:16,phase_switch_supported:true};
  window.teenetEvccDemo=true;
  let inhibited=false,feature=false,accepted=false;
  try{accepted=localStorage.getItem('teenet-demo-terms')==='2026-10-09.2';feature=localStorage.getItem('teenet-demo-evcc-feature')==='true';}catch(e){}
  const transport={shell_rs485_interface:0,meter_rs485_interface:0,house_rs485_interface:0,shell_rs485_host:'',meter_rs485_host:'',house_rs485_host:''};
  const json=(value,status=200)=>new Response(JSON.stringify(value),{status,headers:{'Content-Type':'application/json'}});
  window.fetch=async(input,options={})=>{
    const url=new URL(typeof input==='string'?input:input.url,location.origin);
    if(url.origin===location.origin&&url.pathname==='/api/terms'){
      const data=JSON.parse(options.body||'{}');
      if(data.accepted!==true||data.revision!=='2026-10-09.2')return json({error:'Bestaetigung fehlt'},400);
      accepted=true;try{localStorage.setItem('teenet-demo-terms','2026-10-09.2');}catch(e){}return json({ok:true});
    }
    if(url.origin===location.origin&&options.method==='POST'&&!accepted)return json({error:'Nutzungsbedingungen zuerst bestaetigen.'},403);
    if(url.origin===location.origin&&url.pathname==='/api/evcc/local_release'){
      const data=JSON.parse(options.body||'{}');if(!cfg.enabled||!inhibited||data.release!==true)return json({error:'Keine lokale Sperre'},409);
      inhibited=false;return json({ok:true});
    }
    if(url.origin===location.origin&&url.pathname==='/api/control'&&cfg.enabled&&options.method==='POST')inhibited=true;
    if(url.origin===location.origin&&url.pathname==='/api/evcc/config') {
      if(options.method==='POST') {
        const data=JSON.parse(options.body);
        if(typeof data.enabled!=='boolean'||!/^[a-zA-Z0-9.-]+$/.test(data.shell_host))return json({error:'Adresse pruefen'},400);
        if(data.enabled&&!feature)return json({error:"evcc unter Funktionen einschalten"},409);
        cfg.enabled=data.enabled;inhibited=false;cfg.shell_host=data.shell_host;
        await previous('/api/control',{method:'POST',body:JSON.stringify({mode:'off',enabled:false})});
      }
      return json(cfg);
    }
    if(url.origin===location.origin&&url.pathname==='/api/config'&&options.method==='POST'){
      const data=JSON.parse(options.body||'{}');if('evcc_feature_enabled' in data)feature=!!data.evcc_feature_enabled;
      if(data.basic_mode===true)feature=false;if(!feature){cfg.enabled=false;inhibited=false;}
      for(const k of Object.keys(transport))if(k in data)transport[k]=data[k];
      try{localStorage.setItem('teenet-demo-evcc-feature',String(feature));}catch(e){}
    }
    const response=await previous(input,options);
    if(url.origin===location.origin&&url.pathname==='/api/config'&&response.ok&&options.method!=='POST'){
      const c=await response.json();Object.assign(c,transport,{evcc_feature_enabled:feature});return json(c);
    }
    if(url.origin===location.origin&&url.pathname==='/api/status'&&response.ok) {
      const s=await response.json();Object.assign(s,{evcc_test_enabled:cfg.enabled,evcc_local_stop:inhibited,terms_accepted:accepted,terms_revision:'2026-10-09.2',evcc_lease_expired:false,evcc_status_known:false,evcc_statuscar:'',release_channel:'private-evcc-test'});
      if(!accepted){s.enabled=false;s.mode='off';s.target_current_a=0;s.block_reason='agreement';}
      if(cfg.enabled){s.enabled=false;s.mode='off';s.target_current_a=0;s.block_reason=inhibited?'evcc_local_stop':'evcc_status';}
      return json(s);
    }
    return response;
  };
})();
