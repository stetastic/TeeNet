const fs=require('node:fs'),vm=require('node:vm'),assert=require('node:assert/strict');
const source=fs.readFileSync('main/dashboard.js','utf8'),nodes={},frames=[];
function node(id,tagName='INPUT'){
 const classes=new Set(),n={id,name:id,tagName,dataset:{},attributes:{},children:[],disabled:false,valid:true,validationMessage:'Adresse fehlt',value:'',
 classList:{add:k=>classes.add(k),remove:k=>classes.delete(k),contains:k=>classes.has(k)},
 setAttribute(k,v){this.attributes[k]=v;},getAttribute(k){return this.attributes[k]||null;},removeAttribute(k){delete this.attributes[k];},
 append(child){child.parentElement=this;this.children.push(child);nodes[child.id]=child;},remove(){this.parentElement.children=this.parentElement.children.filter(x=>x!==this);delete nodes[this.id];},
 closest(selector){if(selector==='label')return this.label||null;if(selector==='[hidden]')return this.hiddenAncestor||null;if(selector==='#expert-settings')return this.expert||null;if(selector==='details')return this.section||null;return null;},
 checkValidity(){return this.valid;},focus(){this.focused=true;},scrollIntoView(options){this.scrolled=options;}};nodes[id]=n;return n;
}
const section=node('meter-settings','DETAILS');section.name='';section.open=false;
const label=node('host-label','LABEL');label.firstChild={textContent:'Adresse'};label.parentElement=section;
const host=node('wallbox_meter_host');host.label=label;host.parentElement=label;host.section=section;host.dataset.requiredMessage='Zähler auswählen oder IP eintragen.';host.validity={valueMissing:true};host.attributes['aria-describedby']='host-help';
const max=node('max_charge_a');max.label=label;max.parentElement=label;
const elements=[host,max];elements.namedItem=name=>elements.find(e=>e.name===name);
const ctx={configSaveError:'',window:{matchMedia:()=>({matches:true})},$:id=>id==='config-form'?{elements}:nodes[id],requestAnimationFrame:fn=>frames.push(fn),
 document:{querySelectorAll:()=>Object.values(nodes).filter(n=>n.classList?.contains('config-field-error')),getElementById:id=>nodes[id],createElement:tag=>node('',tag.toUpperCase())},updateUnsavedFunctions(){}};
vm.createContext(ctx);vm.runInContext(source.slice(source.indexOf('function clearConfigFieldError('),source.indexOf("$('config-form').addEventListener('submit'")),ctx);
const flush=()=>{while(frames.length)frames.shift()();};
host.valid=false;assert(!ctx.validateConfigForm());assert(section.open);assert(host.classList.contains('config-field-error'));assert.equal(host.attributes['aria-invalid'],'true');assert.equal(nodes[host.dataset.configErrorId].textContent,host.dataset.requiredMessage);flush();assert(host.focused&&host.scrolled.block==='center');
ctx.clearConfigFieldError(host);assert.equal(host.attributes['aria-describedby'],'host-help');assert(!host.classList.contains('config-field-error'));
host.valid=true;max.disabled=true;ctx.reportConfigSaveError({message:'Maximalstrom zu hoch',field:'max_charge_a'});assert(max.classList.contains('config-field-error'));max.disabled=false;flush();assert(max.focused&&max.scrolled);
ctx.reportConfigSaveError({message:'Verbindung unterbrochen'});assert(!host.classList.contains('config-field-error'),'Unknown errors never blame an arbitrary field');
ctx.clearConfigFieldErrors();assert(!max.classList.contains('config-field-error'));
const hidden=node('phase_feedback_source');hidden.hiddenAncestor={};const choice=node('phase-feedback-choice');choice.parentElement=label;elements.push(hidden);
ctx.reportConfigSaveError({message:'Rückmeldequelle prüfen',field:'phase_feedback_source'});flush();assert(choice.classList.contains('config-field-error')&&choice.focused);assert(!hidden.classList.contains('config-field-error'));
console.log('PASS: invalid fields turn red, open collapsed sections, receive focus and scroll; inline messages, aria help preservation, disabled-during-save recovery, hidden field aliases and unknown-error isolation');
