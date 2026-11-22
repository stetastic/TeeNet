'use strict';
const imageDialog=document.getElementById('image-viewer');
if(imageDialog){
  const full=document.getElementById('image-full');
  const caption=document.getElementById('image-caption');
  const close=document.getElementById('image-close');
  for(const button of document.querySelectorAll('[data-image]'))button.addEventListener('click',()=>{
    const img=button.querySelector('img');
    full.src=img.src;full.alt=img.alt;caption.textContent=button.dataset.image;
    imageDialog.showModal();
  });
  close.addEventListener('click',()=>imageDialog.close());
  imageDialog.addEventListener('click',event=>{if(event.target===imageDialog)imageDialog.close();});
}
const print=document.getElementById('print-help');
if(print)print.addEventListener('click',()=>window.print());
function openHashSection(){
  let id;try{id=decodeURIComponent(location.hash.slice(1));}catch{return;}
  const section=document.getElementById(id);
  if(!section)return;
  const details=section.closest('details');
  if(details){details.open=true;section.scrollIntoView();}
}
window.addEventListener('hashchange',openHashSection);
openHashSection();
