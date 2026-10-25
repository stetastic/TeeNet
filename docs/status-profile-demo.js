/* Synthetic preview only. Keep this outermost, after all demo API shims. */
(() => {
  const previous=window.fetch.bind(window),diagnosticFields=["ap_active","ap_forced","ap_ssid","current_date","evcc_statuscar","hostname","house_a","huawei_model","largest_free_block","last_sent_a","meter_failures","min_free_heap","mqtt_rejected","reset_reason","rs485_modules","saved_ago_s","shell_grid_limit_a","shell_limits_http","shell_max_charge_a","shell_setup_host","station_ip","uptime_s","vehicle_status_age_s","vehicle_status_raw","vehicle_status_reason","wallbox_crc_resyncs","wallbox_last_address","wallbox_last_count","wallbox_last_function","wallbox_last_register","wallbox_other_address","wallbox_reply_age_s","wallbox_requests","wallbox_rx_age_ms","wallbox_rx_bytes","wallbox_rx_tail_hex","wallbox_tx_errors","wallbox_valid_frames","wifi_fallback","wifi_rssi_dbm"];
  window.fetch=async(input,options={})=>{
    const response=await previous(input,options);
    let url;
    try{url=new URL(typeof input==='string'?input:input.url||String(input),window.location.href||window.location.origin);}catch{return response;}
    const method=String(options?.method??input?.method??'GET').toUpperCase();
    if(method!=='GET'||url.origin!==window.location.origin||url.pathname!=='/api/status'||url.searchParams.get('profile')!=='dashboard'||!response.ok)return response;
    let status;
    try{status=await response.clone().json();}catch{return response;}
    if(!status||typeof status!=='object'||Array.isArray(status))return response;
    for(const name of diagnosticFields)delete status[name];
    status.status_profile='dashboard';
    const headers=new Headers(response.headers);
    headers.delete('Content-Length');headers.delete('Content-Encoding');
    return new Response(JSON.stringify(status),{status:response.status,statusText:response.statusText,headers});
  };
})();
