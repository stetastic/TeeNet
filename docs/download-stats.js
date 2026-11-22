/* Public GitHub release counts. No device IDs, cookies or usage reports. */
(()=>{
  'use strict';
  const total=document.getElementById('download-total');
  const status=document.getElementById('download-status');
  if(!total||!status)return;
  const key='teenet-release-downloads-v1';
  const ttl=120000;
  const format=new Intl.NumberFormat('de-DE');
  const render=data=>{
    total.textContent=format.format(data.total);
    status.textContent='GitHub-Dateidownloads · Mehrfachdownloads zählen mit.';
    document.querySelectorAll('[data-download-asset]').forEach(el=>{
      const name=el.dataset.downloadAsset;
      if(data.urls?.[name]){el.href=data.urls[name];el.removeAttribute('download');}
    });
    document.querySelectorAll('[data-count-for]').forEach(el=>{
      const n=data.assets[el.dataset.countFor];
      el.textContent=Number.isSafeInteger(n)?'Downloads: '+format.format(n):'Downloads: noch nicht verfügbar';
    });
  };
  const load=async()=>{
    try{
      try{
        const cached=JSON.parse(sessionStorage.getItem(key));
        if(cached&&Number.isSafeInteger(cached.total)&&cached.assets&&Date.now()-cached.at>=0&&Date.now()-cached.at<ttl){render(cached);return;}
      }catch{}
      const controller=new AbortController();
      const timer=setTimeout(()=>controller.abort(),8000);
      let releases;
      try{
        const response=await fetch('https://api.github.com/repos/stetastic/TeeNet/releases?per_page=100',{
          headers:{Accept:'application/vnd.github+json'},credentials:'omit',referrerPolicy:'no-referrer',signal:controller.signal
        });
        if(!response.ok)throw new Error('GitHub unavailable');
        releases=await response.json();
      }finally{clearTimeout(timer);}
      if(!Array.isArray(releases)||!releases.length||releases.length===100)throw new Error('Incomplete release list');
      const data={total:0,assets:{},urls:{},at:Date.now()};
      for(const release of releases){
        if(release.draft)continue;
        for(const asset of release.assets||[]){
          if(!/^TeeNet-.+\.(bin|zip)$/.test(asset.name))continue;
          if(!Number.isSafeInteger(asset.download_count)||asset.download_count<0)throw new Error('Invalid count');
          data.total+=asset.download_count;
          data.assets[asset.name]=(data.assets[asset.name]||0)+asset.download_count;
          const prefix='https://github.com/stetastic/TeeNet/releases/download/';
          if(typeof asset.browser_download_url==='string'&&asset.browser_download_url.startsWith(prefix)&&asset.browser_download_url.endsWith('/TeeNet/'+asset.name))data.urls[asset.name]=asset.browser_download_url;
        }
      }
      if(!Object.keys(data.assets).length||!Number.isSafeInteger(data.total))throw new Error('No assets');
      render(data);
      try{sessionStorage.setItem(key,JSON.stringify(data));}catch{}
    }catch{
      total.textContent='—';
      status.textContent='Downloadzähler gerade nicht verfügbar. Herunterladen ist weiterhin möglich.';
    }
  };
  load();
})();
