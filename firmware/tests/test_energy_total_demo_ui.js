'use strict';
const assert=require('node:assert/strict');
const fs=require('node:fs');
const path=require('node:path');
const vm=require('node:vm');

// This stub only exposes the nodes used by the demand layer. The real model
// runs with the UI so a stale number cannot pass merely because a mock returns it.
function node(){
 return {hidden:false,textContent:'',dataset:{},children:[],attributes:{},
  setAttribute(name,value){this.attributes[name]=value;},
  append(child){this.children.push(child);}};
}
const overview=node(),sources=node(),house=node(),root=node();
root.dataset.paused='false';
root.querySelector=selector=>({'.en-overview':overview,'.en-sources':sources})[selector]??null;
overview.insertBefore=(child,before)=>{assert.equal(before,sources);overview.children.push(child);};
const created=[];
const document={documentElement:{dataset:{dashboardView:'standard',dashboardDensity:'standard'}},
 getElementById(id){return {'teenet-house-mood':root,'house-card':house}[id]??null;},
 createElement(tag){
  assert.equal(tag,'div');
  const element=node(),fields=new Map();
  Object.defineProperty(element,'innerHTML',{set(markup){
   for(const [,attribute] of markup.matchAll(/\b(data-demand-[a-z-]+)(?:\s|>)/g))fields.set('['+attribute+']',node());
   const battery=fields.get('[data-demand-battery]');
   if(battery){const value=node();battery.querySelector=selector=>selector==='b'?value:null;}
  }});
  element.querySelector=selector=>fields.get(selector)??null;
  created.push(element);return element;
 }};
const calls=[],energy={update(s,online){calls.push({receiver:this,s,online});}};
let observer;
class MutationObserver{
 constructor(callback){observer=callback;}
 observe(target,options){
  assert.equal(target,document.documentElement);
  assert.equal(options.attributes,true);
  assert.deepEqual(Array.from(options.attributeFilter),['data-dashboard-view','data-dashboard-density']);
 }
}
const context=vm.createContext({document,window:{TeeNetEnergy:energy},MutationObserver,
 ResizeObserver:class{observe(){}},requestAnimationFrame(){}});
for(const filename of ['energy-total-demo.js','energy-total-demo-ui.js']){
 vm.runInContext(fs.readFileSync(path.join(__dirname,'../tools',filename),'utf8'),context,{filename});
}
assert.equal(created.length,0,'the energy layer adds no total-demand box');
assert.equal(house.children.length,0,'the Standard house card adds no total-demand row');
const base={basic_mode:false,zero_feed_enabled:true,house_meter_ok:true,house_power_w:1990};
for(const online of [true,false]){
 energy.update(base,online);
 assert.equal(calls.at(-1).receiver,energy);
 assert.equal(calls.at(-1).s,base);
 assert.equal(calls.at(-1).online,online);
}
const count=calls.length;
for(const [name,next] of [['dashboardDensity','compact'],['dashboardView','graphic']]){
 document.documentElement.dataset[name]=next;observer();
}
assert.equal(calls.length,count,'layout changes must not inject or reinterpret status readings');
console.log('Demo energy paths: no total-demand box, unchanged status forwarding and layout-only preference updates passed.');
