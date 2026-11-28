const {WASI}=require('node:wasi');
const fs=require('node:fs');
const path=require('node:path');

async function main(){
  const file=path.resolve(process.argv[2]);
  const wasi=new WASI({version:'preview1',args:[file,...process.argv.slice(3)],env:{}});
  const module=await WebAssembly.compile(fs.readFileSync(file));
  wasi.start(new WebAssembly.Instance(module,{wasi_snapshot_preview1:wasi.wasiImport}));
}
main().catch(error=>{console.error(error);process.exitCode=1;});
