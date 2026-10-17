(()=>{const $=id=>document.getElementById(id);
const donateUrl=$('donate-link').href;
let qrLoading=false;
function showDonate(){
  $('donate-dialog').showModal();
  if(qrLoading||$('donate-qr').querySelector('svg'))return;
  qrLoading=true;
  const script=document.createElement('script');script.src='/TeeNet/qrcode.js';
  script.onload=()=>{try{const qr=qrcode(0,'M');qr.addData(donateUrl);qr.make();$('donate-qr').innerHTML=qr.createSvgTag({cellSize:5,margin:4,scalable:true});}catch(error){$('donate-qr').textContent='QR-Code konnte nicht erzeugt werden.';}finally{qrLoading=false;}};
  script.onerror=()=>{qrLoading=false;$('donate-qr').textContent='QR-Code konnte nicht geladen werden.';};
  document.head.append(script);
}
$('donate-open').addEventListener('click',showDonate);
$('donate-close').addEventListener('click',()=>$('donate-dialog').close());
$('donate-dialog').addEventListener('click',e=>{if(e.target===$('donate-dialog'))$('donate-dialog').close();});
})();
