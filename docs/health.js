'use strict';
/* Display and support helpers never feed retained values back into control. */
window.TeeNetHealth=(()=>{
  let samples={},sourceKey='';
  function observe(s,now=Date.now()){
    const key=[s.wallbox_meter_type,s.house_meter_type,s.battery_source].join('|');
    if(sourceKey!==key){samples={};sourceKey=key;}
    const values={wallbox:s.wallbox_w??s.estimate_w,house:s.house_power_w,battery:s.battery_soc_pct,pv:s.pv_generation_w};
    const valid={wallbox:s.feedback_ok,house:s.house_meter_ok,battery:s.battery_soc_ok??s.battery_ok,pv:Number.isFinite(s.pv_generation_w)};
    for(const name of Object.keys(values))if(valid[name]&&Number.isFinite(values[name])){
      let value=values[name];
      if(name==='wallbox'&&Array.isArray(s.actual_a)&&s.actual_a.length===3&&s.actual_a.every(a=>Number.isFinite(a)&&a<1))value=0;
      samples[name]={value,at:now-Math.max(0,s.measurement_age_s?.[name]??0)*1000};
    }
  }
  function reading(s,name,online=true,now=Date.now()){
    const sample=samples[name];if(!sample)return {value:null,fresh:false,age:null};
    const valid={wallbox:s.feedback_ok,house:s.house_meter_ok,battery:s.battery_soc_ok??s.battery_ok,pv:Number.isFinite(s.pv_generation_w)};
    return {value:sample.value,fresh:!!online&&!!valid[name],age:Math.max(0,Math.floor((now-sample.at)/1000))};
  }
  function uptimeText(seconds){if(!Number.isFinite(seconds)||seconds<0)return '—';const s=Math.floor(seconds),days=Math.floor(s/86400),clock=[Math.floor(s/3600)%24,Math.floor(s/60)%60,s%60].map(n=>String(n).padStart(2,'0')).join(':');return `${days?days+' d · ':''}${clock}`;}
  function ageText(age){return age===null?'kein Messwert':age<60?`${age} s alt`:age<3600?`${Math.floor(age/60)} min alt`:`${Math.floor(age/3600)} h alt`;}
  function setupChecks(s,c){
    const rows=[['Heimnetz',s.wifi_ok,'WLAN auswählen und speichern.'],['Shell-Wallbox',s.wallbox_ok,'Versorgung und RS485-Anschluss prüfen.'],['Wallbox-Zähler',s.feedback_ok,'Zählermodell und Verbindung prüfen.'],['Uhrzeit',s.clock_ok,'Internetverbindung prüfen.']];
    if(c.mqtt_enabled)rows.push(['MQTT',s.mqtt_ok,'Brokeradresse und Zugang prüfen.']);
    if(c.zero_feed_enabled||c.grid_guard_enabled)rows.push(['Hauszähler',s.house_meter_ok,'Zähler auswählen und Messwerte prüfen.']);
    if(c.battery_protect)rows.push(['Hausakku',s.battery_ok,'SOC sowie Lade- und Entladeleistung prüfen.']);
    if(c.huawei_enabled)rows.push(['Huawei',s.huawei_ok,'Modbus TCP und Geräteadresse prüfen.']);
    if(c.grid_guard_enabled)rows.push(['Haus-Phasenströme',s.house_current_ok,'Drei aktuelle Phasenströme erforderlich; sonst gilt die 8-kW-Grenze.']);
    return rows.map(([name,ok,help])=>({name,ok:!!ok,help}));
  }
  function sanitize(value){
    if(Array.isArray(value))return value.map(sanitize);
    if(!value||typeof value!=='object')return value;
    const privateKey=/pass|token|ssid|username|hostname|_host$|_uri$|_prefix$|station_ip|rx_tail|power_path|setup_host|^energy$/i;
    return Object.fromEntries(Object.entries(value).filter(([key])=>!privateKey.test(key)).map(([key,v])=>[key,sanitize(v)]));
  }
  return {observe,reading,ageText,uptimeText,setupChecks,sanitize};
})();
