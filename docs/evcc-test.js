/* Device-specific evcc configuration; access credentials stay private. */
(() => {
  const byId=id=>document.getElementById(id);
  let config=null,loading=false,edited=false,loaded=false;
  const note=text=>{byId('evcc-note').textContent=text;};
  const yaml=(cfg,host)=>`# TeeNet: Eintraege in die vorhandene evcc-Konfiguration uebernehmen.
# Bestehende Zaehler, Fahrzeuge und andere Ladepunkte erhalten.
# Die Adresse entspricht der beim Download geoeffneten TeeNet-Oberflaeche.
chargers:
  - name: teenet
    type: custom
    status:
      source: http
      uri: http://${host}/api/evcc/status
      auth: &teenet_auth
        type: bearer
        token: "${cfg.key}"
      jq: .status
      timeout: 5s
    enabled:
      source: http
      uri: http://${host}/api/evcc/enabled
      auth: *teenet_auth
      timeout: 5s
    enable:
      source: http
      uri: http://${host}/api/evcc/enable
      method: POST
      headers:
        - content-type: application/json
      auth: *teenet_auth
      body: '{"enabled": {{ .enable }}}'
      timeout: 5s
    maxcurrent:
      source: http
      uri: http://${host}/api/evcc/current
      method: POST
      headers:
        - content-type: application/json
      auth: *teenet_auth
      body: '{"current_a": {{ .maxcurrent }}}'
      timeout: 5s
    maxcurrentmillis:
      source: http
      uri: http://${host}/api/evcc/current
      method: POST
      headers:
        - content-type: application/json
      auth: *teenet_auth
      body: '{"current_a": {{ .maxcurrentmillis }}}'
      timeout: 5s
    power:
      source: http
      uri: http://${host}/api/evcc/status
      auth: *teenet_auth
      jq: 'if .meter_ok then .power_w else error("Wallbox-Messwerte fehlen") end'
      timeout: 5s
    energy:
      source: http
      uri: http://${host}/api/evcc/status
      auth: *teenet_auth
      jq: .energy_kwh
      timeout: 5s
${cfg.phase_switch_supported?`    tos: true
    phases1p3p:
      source: http
      uri: http://${host}/api/evcc/phases
      method: POST
      headers:
        - content-type: application/json
      auth: *teenet_auth
      body: '{"phases": {{ .phases }}}'
      timeout: 5s
    getphases:
      source: http
      uri: http://${host}/api/evcc/phases
      auth: *teenet_auth
      timeout: 5s
`:''}

loadpoints:
  - title: TeeNet
    charger: teenet
    mode: off
    phases: ${cfg.phase_switch_supported?0:cfg.phases}
    mincurrent: ${cfg.min_current_a}
    maxcurrent: ${cfg.max_current_a}

# evcc waehlt die Phasen; TeeNet fuehrt den stromlosen Wechsel aus.
# Die Datei enthaelt einen Zugangsschluessel: privat aufbewahren.
`;
  async function load() {
    if(loading||!online||!token)return;
    loading=true;
    try {
      config=await api('/api/evcc/config',undefined,8000,true);loaded=true;
      if(!edited){byId('evcc-active').checked=config.enabled;byId('evcc-host').value=config.shell_host;}
      note(config.enabled?(config.phase_switch_supported?'evcc steuert Ladeleistung und Phasenauswahl.':'evcc steuert die Ladeleistung. Phasenumschaltung ist in TeeNet ausgeschaltet.'):'evcc aus · TeeNet steuert wie bisher.');
    } catch(error){note(error.message);} finally{loading=false;}
  }
  function ownerView(s) {
    if(!s)return;
    const active=!!s.evcc_test_enabled;
    byId('mqtt-evcc-note').hidden=!active;
    byId('evcc-owner-banner').hidden=!active;
    byId('evcc-owner-banner').closest('.control-panel').classList.toggle('evcc-controlled',active);
    if(active)byId('apply-control').textContent=s.phase_test_active?'Schütztest · Stoppen':s.evcc_local_stop?'evcc freigeben':'Lokal stoppen';
    document.querySelector('.mode-picker').hidden=active||!!s.basic_mode;
    document.querySelectorAll('[data-mode]').forEach(el=>{el.disabled=active||!online;});
    byId('current').disabled=active||!online||busy;
    if(active)byId('apply-control').disabled=!online||busy;
    if(active) {
      byId('plan-panel').hidden=true;
      byId('mode').value='manual';
      document.querySelectorAll('[data-mode]').forEach(el=>el.setAttribute('aria-pressed',String(el.dataset.mode==='manual')));
      byId('manual-current-label').hidden=true;
      byId('pv-allocation-status').hidden=true;
      byId('battery-card').dataset.inactive='true';
      document.querySelectorAll('.battery-choice').forEach(el=>el.hidden=true);
      byId('battery-reserve-slider').disabled=true;
    }
    byId('evcc-save').disabled=!online||loading;
    byId('evcc-download').disabled=!online||!loaded||loading;
    if(active && s.evcc_local_stop)note('Lokal gesperrt. Mit „evcc freigeben“ wieder entsperren.');
    else if(active && s.evcc_lease_expired)note('evcc-Verbindung ausgefallen. Ein neuer Startbefehl von evcc ist erforderlich.');
    else if(active && !s.evcc_status_known)note('Fahrzeugstatus unbekannt. Der Adapter meldet HTTP 503; ein abgezogener Stecker wird nicht angenommen.');
    if(!loaded&&location.hash.includes('settings'))load();
  }
  byId('evcc-active').addEventListener('change',()=>{edited=true;});
  byId('control-form').addEventListener('submit',event=>{
    if(!state?.evcc_test_enabled||state.phase_test_active)return;
    event.preventDefault();event.stopImmediatePropagation();
    if(busy||!online)return;
    action(async()=>{
      const release=!!state.evcc_local_stop;
      await api(release?'/api/evcc/local_release':'/api/control',release?{release:true}:{enabled:false,mode:'off'});
      await pollStatus();toast(release?'evcc freigegeben. Wartet auf einen neuen Startbefehl.':'Lokal gestoppt. Weitere Starts durch evcc sind gesperrt.');
    });
  },true);
  byId('evcc-host').addEventListener('input',()=>{edited=true;});
  byId('evcc-save').addEventListener('click',async()=>{
    if(loading)return;loading=true;
    try {
      config=await api('/api/evcc/config',{enabled:byId('evcc-active').checked,shell_host:byId('evcc-host').value.trim()});
      edited=false;loaded=true;await pollStatus();
      note(config.enabled?'Gespeichert. Jetzt Konfiguration herunterladen und evcc einrichten.':'evcc aus. TeeNet bleibt bis zum nächsten Start gestoppt.');
    }catch(error){note(error.message);}finally{loading=false;ownerView(state||{});}
  });
  byId('evcc-download').addEventListener('click',async()=>{
    if(loading||!online)return;
    if(window.teenetEvccDemo){note('Die Demo enthält keinen echten Schlüssel. Konfiguration später direkt vom ESP herunterladen.');return;}
    loading=true;byId('evcc-download').disabled=true;
    try{
      config=await api('/api/evcc/config',undefined,8000,true);
      const blob=new Blob([yaml(config,location.host)],{type:'text/yaml;charset=utf-8'}),url=URL.createObjectURL(blob),a=document.createElement('a');
      a.href=url;a.download='TeeNet-evcc.yaml';a.click();setTimeout(()=>URL.revokeObjectURL(url),1000);
      note('Konfiguration mit aktueller TeeNet-Adresse und Ladegrenzen heruntergeladen.');
    }catch(error){note(error.message);}finally{loading=false;ownerView(state||{});}
  });
  window.addEventListener('teenet-status',event=>ownerView(event.detail));
  window.addEventListener('hashchange',()=>{if(location.hash.includes('evcc-settings')||location.hash.includes('settings'))load();});
  window.teenetEvccTest={yaml,updateWrites:ownerView};
})();
