/* Demo-only notice. Confirm once per tab session; never change device settings. */
(()=>{
 'use strict';
 const dialog=document.getElementById('demo-welcome'),button=document.getElementById('demo-welcome-ok');
 if(!dialog||!button)return;
 const key='teenet-demo-welcome-v1';
 let confirmed=false;
 try{confirmed=sessionStorage.getItem(key)==='1';}catch{}
 function show(){
  if(confirmed)return;
  // Do not stack the notice over setup or other confirmation dialogs.
  const other=document.querySelector('dialog[open]:not(#demo-welcome)');
  if(other){if(dialog.open)dialog.close();return;}
  if(!dialog.open)dialog.showModal();
 }
 const observer=new MutationObserver(show);
 if(!confirmed)observer.observe(document.body,{subtree:true,attributes:true,attributeFilter:['open']});
 dialog.addEventListener('cancel',event=>event.preventDefault());
 button.addEventListener('click',()=>{
  confirmed=true;observer.disconnect();
  try{sessionStorage.setItem(key,'1');}catch{}
  dialog.close();
 });
 show();
})();
