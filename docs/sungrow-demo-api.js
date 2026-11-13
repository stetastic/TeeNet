/* Demo only: selection and synthetic readings. Never contacts an inverter. */
(()=>{
 const previous=window.fetch.bind(window),json=(v,status=200)=>new Response(JSON.stringify(v),{status,headers:{'Content-Type':'application/json'}});
 window.fetch=async(input,options={})=>{
  const url=new URL(typeof input==='string'?input:input.url,location.origin),method=String(options.method||input?.method||'GET').toUpperCase();
  if(url.origin!==location.origin)return previous(input,options);
  if(url.pathname==='/api/config'&&method==='POST'){
   let body;try{body=JSON.parse(options.body||'{}');}catch{return json({error:'JSON ungültig.'},400);}
   const c={...await (await previous('/api/config')).json(),...body};
   const error=(field,message)=>json({error:message,field},400);
   if(c.sungrow_enabled&&!c.basic_mode){
    if(typeof c.sungrow_host!=='string'||!c.sungrow_host||!/^[A-Za-z0-9.-]{1,63}$/.test(c.sungrow_host))return error('sungrow_host','IP-Adresse oder Hostname des Sungrow eintragen.');
    if(!Number.isInteger(c.sungrow_profile)||c.sungrow_profile<0||c.sungrow_profile>3)return error('sungrow_model','Sungrow-Modell wählen.');
    if(!Number.isInteger(c.sungrow_port)||c.sungrow_port<1||c.sungrow_port>65535)return error('sungrow_port','TCP-Port von 1 bis 65535 wählen.');
    if(!Number.isInteger(c.sungrow_unit_id)||c.sungrow_unit_id<1||c.sungrow_unit_id>247)return error('sungrow_unit_id','Modbus-Adresse von 1 bis 247 wählen.');
    if(c.sungrow_battery&&[1,2].includes(c.sungrow_profile))return error('sungrow_model','Reine PV-Wechselrichter liefern keinen Hausakku.');
    if(c.house_meter_type==='sungrow'&&c.sungrow_profile===1)return error('house_meter_type','Für dieses Modell einen separaten Hauszähler wählen.');
    if(c.huawei_enabled&&((c.huawei_pv&&c.sungrow_pv)||(c.huawei_battery&&c.sungrow_battery)))return error('sungrow_enabled','Je Messwert nur Huawei oder Sungrow aktivieren.');
   }
  }
  const response=await previous(input,options);
  if(url.pathname!=='/api/status'||method!=='GET'||!response.ok)return response;
  const state=await response.json(),c=await (await previous('/api/config')).json();
  if(!c.sungrow_enabled||c.basic_mode)return json(state);
  const valid=!!c.sungrow_host;
  Object.assign(state,{sungrow_enabled:true,sungrow_ok:valid,sungrow_model:c.sungrow_model});
  if(c.sungrow_pv)state.pv_source='Sungrow';
  if(c.sungrow_battery)state.battery_source='Sungrow';
  return json(state);
 };
})();
