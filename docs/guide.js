'use strict';
// Preserve links from older help pages and downloaded documents.
const helpAliases={wiring:'anschliessen',setup:'einrichtung',charging:'laden',battery:'hausakku',statistics:'verbrauch',sources:'zusatzfunktionen',troubleshooting:'probleme',updates:'wartung','phase-switching':'relais-phasen'};
function openSection(){
  let id;try{id=decodeURIComponent(location.hash.slice(1));}catch{return;}
  const target=document.getElementById(helpAliases[id]||id);
  if(!target)return;
  for(let node=target;node;node=node.parentElement)if(node.tagName==='DETAILS')node.open=true;
  target.scrollIntoView({block:'start'});
}
addEventListener('hashchange',openSection);
openSection();

const imageViewer=document.getElementById('image-viewer');
if(imageViewer){
  const full=document.getElementById('image-full');
  for(const button of document.querySelectorAll('[data-image]'))button.addEventListener('click',()=>{
    const source=button.querySelector('img'),vector=button.querySelector('svg');
    if(!source&&!vector)return;
    if(source){full.src=source.src;full.alt=source.alt;}
    else{full.src='data:image/svg+xml;charset=utf-8,'+encodeURIComponent(new XMLSerializer().serializeToString(vector));full.alt=vector.querySelector('desc')?.textContent||button.dataset.image;}
    document.getElementById('image-caption').textContent=button.dataset.image;
    imageViewer.showModal();
  });
  document.getElementById('image-close').addEventListener('click',()=>imageViewer.close());
  imageViewer.addEventListener('click',event=>{if(event.target===imageViewer)imageViewer.close();});
}
document.getElementById('print-help')?.addEventListener('click',()=>window.print());
let printState;
addEventListener('beforeprint',()=>{
  if(printState)return;
  printState=[...document.querySelectorAll('.content details')].map(node=>[node,node.open]);
  for(const [node] of printState)node.open=true;
});
addEventListener('afterprint',()=>{
  for(const [node,open] of printState||[])node.open=open;
  printState=undefined;
});
