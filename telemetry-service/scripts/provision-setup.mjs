import {randomBytes} from 'node:crypto';
import {spawnSync} from 'node:child_process';
const token=randomBytes(32).toString('hex');
const result=spawnSync(process.execPath,['node_modules/wrangler/bin/wrangler.js','secret','put','SETUP_TOKEN','--config','wrangler.admin.json'],{input:token+'\n',encoding:'utf8',env:{...process.env,WRANGLER_SEND_METRICS:'false'}});
if(result.status!==0){process.stderr.write('Cloudflare setup-secret provisioning failed.\n');process.exit(1);}
process.stdout.write('SETUP_URL=https://teenet-statistik.stetastic.workers.dev/setup#'+token+'\n');
