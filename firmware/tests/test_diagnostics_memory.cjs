const fs=require('node:fs'),vm=require('node:vm'),assert=require('node:assert/strict');
const source=fs.readFileSync('main/dashboard.js','utf8');
const line=source.split(/\r?\n/).find(value=>value.includes("diagnostic('Arbeitsspeicher'"));
assert(line,'exercise the actual memory diagnostic rendering');
function render(s){
 const context={s,diagnostics:[],format:(value,digits)=>value.toFixed(digits),diagnostic:(label,value)=>({label,value}),Number};
 vm.runInNewContext(line,context);return context.diagnostics;
}
assert.deepEqual(render({min_free_heap:1024,largest_free_block:2048}),[{label:'Arbeitsspeicher',value:'Minimum 1 KiB · größter Block 2 KiB'}]);
assert.deepEqual(render({free_heap:4096,min_free_heap:1024,largest_free_block:2048}),[{label:'Arbeitsspeicher',value:'4 KiB frei · Minimum 1 KiB · größter Block 2 KiB'}]);
assert.equal(render({min_free_heap:1024}).length,0);
assert.equal(render({min_free_heap:NaN,largest_free_block:2048}).length,0);
console.log('PASS: memory diagnostics show firmware-provided metrics and include current free heap only when available');
