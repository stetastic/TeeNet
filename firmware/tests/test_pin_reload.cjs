const fs=require('node:fs'),vm=require('node:vm'),assert=require('node:assert/strict');
const js=fs.readFileSync('main/dashboard.js','utf8');
function select(defaultPin){let selected=String(defaultPin);return {tagName:'SELECT',type:'select-one',options:[{value:String(defaultPin)}],hasAttribute(){return false;},append(option){this.options.push(option);},get value(){return selected;},set value(value){selected=this.options.some(o=>o.value===String(value))?String(value):'';}};}
const tx=select(17),rx=select(18),elements={wallbox_tx_pin:tx,wallbox_rx_pin:rx,namedItem(name){return this[name];}};
const nodes={'config-form':{elements},'battery-reserve-slider':{}};
const backend={wallbox_tx_pin:6,wallbox_rx_pin:5,battery_reserve_soc:35};
const ctx=vm.createContext({$:id=>nodes[id],configBaseline:{},api:async()=>backend,document:{createElement(){return {};}},Array,Number,String,Object,Math,updateBatterySlider(){},updateMeterFields(){},updateExpertMode(){},updateEquipment(){},secretPlaceholders(){}});
const begin=js.indexOf('async function loadConfig('),end=js.indexOf('function updateBatterySlider()',begin);vm.runInContext(js.slice(begin,end),ctx);
(async()=>{await vm.runInContext('loadConfig()',ctx);assert.equal(tx.value,'6');assert.equal(rx.value,'5');assert.equal(tx.options.length,2);await vm.runInContext('loadConfig()',ctx);assert.equal(rx.value,'5');assert.equal(tx.options.length,2);console.log('PASS: fresh page/reboot preserves TX6/RX5 before generating pin choices');})().catch(e=>{console.error(e);process.exitCode=1;});
