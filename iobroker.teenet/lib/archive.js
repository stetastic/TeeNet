'use strict';

const PREFIX = '0_userdata.0.TeeNet.Archiv.';
const DEVICE = /^teenet(?:_[a-f0-9]+)?$/;
const PERIODS = { today: 'Heute', month: 'Monat', year: 'Jahr', total: 'Seit Aufzeichnung' };
const FIELDS = {
    energy_kwh: ['Ladeenergie', 'kWh'], solar_kwh: ['Solar / Hausakku', 'kWh'], grid_kwh: ['Netz', 'kWh'],
    cost_eur: ['Ladekosten', '€'], solar_cost_eur: ['Kosten Solar / Hausakku', '€'], grid_cost_eur: ['Netzkosten', '€'],
};
const interval = 300000;
const recoveryInterval = 3600000;
const retryInterval = 30000;
const calendar = new Intl.DateTimeFormat('sv-SE',{timeZone:'Europe/Berlin',year:'numeric',month:'2-digit',day:'2-digit'});
const cursorPattern = /^(?:0|[1-9]\d{0,9})_(?:0|[1-9]\d{0,9})$/;
const markerPattern = /^teenet(?:_[a-f0-9]+)?:(?:0|[1-9]\d{0,9})_(?:0|[1-9]\d{0,9})$/;
const dateKey = now => calendar.format(now).replaceAll('-','');

function endpoint(host) {
    if (typeof host !== 'string' || !/^[\w.:[\]-]+$/.test(host.trim())) throw Error('Ungültige TeeNet-Geräteadresse');
    return new URL('/api/export.json', 'http://' + host.trim());
}
function validate(row) {
    if (!row || !Number.isSafeInteger(row.id) || row.id < 1 ||
        !['date','start_epoch','end_epoch','charging_s','flags'].every(k => Number.isSafeInteger(row[k]) && row[k] >= 0) ||
        (row.date && !/^\d{4}(0[1-9]|1[0-2])(0[1-9]|[12]\d|3[01])$/.test(String(row.date))) ||
        !Object.keys(FIELDS).every(k => Number.isFinite(row[k]) && row[k] >= 0)) throw Error('Ungültige Ladesitzung');
    return row;
}
const key = row => row.start_epoch + '_' + row.id;
const monthOf = row => row.date ? String(row.date).slice(0, 6) : 'ohneDatum';
function parseMonth(state, month) {
    if (!state) return {};
    const entries = JSON.parse(state.val);
    if (!entries || typeof entries !== 'object' || Array.isArray(entries)) throw Error('Ungültiges Monatsarchiv ' + month);
    for (const [k, row] of Object.entries(entries)) {
        validate(row);
        if (k !== key(row) || monthOf(row) !== month) throw Error('Ungültiger Sitzungsschlüssel in ' + month);
    }
    return entries;
}
const empty = () => ({ sessions: 0, ...Object.fromEntries(Object.keys(FIELDS).map(k => [k, 0])) });
function add(target, item) {
    target.sessions += item.sessions ?? 1;
    for (const k of Object.keys(FIELDS)) target[k] += item[k];
}
const rounded = n => Math.round(n * 1e6) / 1e6;
function totals(sum) {
    return { ...Object.fromEntries(Object.entries(sum).map(([k,n]) => [k, rounded(n)])),
        solar_pct: sum.energy_kwh ? Math.round(1000 * sum.solar_kwh / sum.energy_kwh) / 10 : 0 };
}
function summarize(entries) {
    const sum = empty(), days = new Map();
    const recent = Object.values(entries).sort((a,b) => b.start_epoch-a.start_epoch || b.id-a.id);
    for (const row of recent) {
        add(sum, row);
        if (!days.has(row.date)) days.set(row.date, empty());
        add(days.get(row.date), row);
    }
    return { sum, days, recent: recent.slice(0, 12) };
}
function aggregate(months, date) {
    const periods = Object.fromEntries(Object.keys(PERIODS).map(k => [k, empty()]));
    const timeline = [], recent = [];
    for (const [month, data] of months) {
        add(periods.total, data.sum);
        if (month.startsWith(date.slice(0,4))) add(periods.year, data.sum);
        if (month === date.slice(0,6)) {
            add(periods.month, data.sum);
            if (data.days.has(Number(date))) add(periods.today, data.days.get(Number(date)));
        }
        timeline.push({ month, ...totals(data.sum) });
        recent.push(...data.recent);
    }
    return { periods, timeline: timeline.sort((a,b) => b.month.localeCompare(a.month)),
        recent: recent.sort((a,b) => b.start_epoch-a.start_epoch || b.id-a.id).slice(0,12) };
}
function stateObject(name, type, role, unit) {
    return { type: 'state', common: {name, type, role, read: true, write: false, ...(unit ? {unit} : {})}, native: {} };
}
class SessionArchive {
    constructor(io) {
        this.io=io;
        this.months=new Map();
        this.stopped=false;
        this.nextRead=0;
    }
    async start() {
        const host=this.io.config.archiveHost?.trim();
        if (!host) return;
        this.url=endpoint(host);
        const io=this.io;
        await io.setObjectNotExistsAsync('stats',{type:'channel',common:{name:'Ladeauswertung'},native:{}});
        for (const [period,label] of Object.entries(PERIODS)) {
            await io.setObjectNotExistsAsync('stats.'+period,{type:'channel',common:{name:label},native:{}});
            for (const [k,[name,unit]] of Object.entries({...FIELDS,sessions:['Ladesitzungen',''],solar_pct:['Solaranteil','%']})) {
                await io.setObjectNotExistsAsync('stats.'+period+'.'+k,stateObject(name,'number','value',unit));
            }
        }
        for(const [k,name,type,role] of [
            ['device','Archiviertes Gerät','string','text'],['lastSync','Zuletzt abgeglichen','number','date'],
            ['lastError','Archivstatus','string','text'],['months','Monatsauswertung','string','json'],
            ['recentSessions','Letzte Ladesitzungen','string','json'],
            ['cursor','Zuletzt archivierte Ladesitzung','string','text']]){
            await io.setObjectNotExistsAsync('stats.'+k,stateObject(name,type,role));
        }
        const last=await io.getStateAsync('stats.device');
        if(last && DEVICE.test(last.val)) this.previous=last.val;
        const saved=(await io.getStateAsync('stats.cursor'))?.val;
        if(typeof saved==='string' && markerPattern.test(saved) && saved.startsWith(this.previous+':')) this.cursor=saved;
        this.started=true;
        this.requestSync();
    }
    async loadDevice(device) {
        if(this.device===device) return;
        const prefix=PREFIX+device+'.';
        const states=await this.io.getForeignStatesAsync(prefix+'*');
        const months=new Map();
        for(const [id,state] of Object.entries(states||{})){
            const month=id.slice(prefix.length);
            if(id.startsWith(prefix)&&/^(\d{6}|ohneDatum)$/.test(month)) months.set(month,summarize(parseMonth(state,month)));
        }
        this.months=months; this.device=device;
    }
    async ingest(payload) {
        if(payload?.schema!==1 || !DEVICE.test(payload.device) || !Array.isArray(payload.sessions)) throw Error('Unbekanntes TeeNet-Archivformat');
        const groups=new Map();
        for(const row of payload.sessions){
            validate(row);
            const month=monthOf(row);
            if(!groups.has(month)) groups.set(month,[]);
            groups.get(month).push(row);
        }
        await this.loadDevice(payload.device);
        for(const [month,rows] of groups){
            const id=PREFIX+this.device+'.'+month;
            const state=await this.io.getForeignStateAsync(id);
            const entries=parseMonth(state,month);
            for(const row of rows) entries[key(row)]=row;
            const next=JSON.stringify(entries);
            if(!state) await this.io.setForeignObjectNotExistsAsync(id,stateObject('TeeNet Ladesitzungen '+month,'string','json'));
            if(next!==state?.val) await this.io.setForeignStateAsync(id,{val:next,ack:true});
            this.months.set(month,summarize(entries));
        }
        await this.set('device',this.device);
    }
    set(id,val) { return this.io.setStateChangedAsync('stats.'+id,{val,ack:true}); }
    async publish(now) {
        // TeeNet timestamps its sessions in CET/CEST even if the ioBroker host runs in UTC.
        const date=dateKey(now);
        const {periods,recent,timeline}=aggregate(this.months,date);
        for(const [period,sum] of Object.entries(periods)) {
            for(const [k,val] of Object.entries(totals(sum))) await this.set(period+'.'+k,val);
        }
        await this.set('months',JSON.stringify(timeline));
        await this.set('recentSessions',JSON.stringify(recent));
        this.publishedDate=date;
    }

    async read(after) {
        const url=new URL(this.url);
        url.searchParams.set('after',after);
        this.abort=new AbortController();
        const response=await fetch(url,{redirect:'error',signal:AbortSignal.any([this.abort.signal,AbortSignal.timeout(10000)])});
        if(!response.ok){await response.body?.cancel();throw Error('HTTP '+response.status);}
        return response.json();
    }

    async sync() {
        const [device,after]=this.cursor ? this.cursor.split(':') : ['', '0_0'];
        let body=await this.read(after);
        if(after!=='0_0' && body?.cursor!==undefined && body.device!==device) body=await this.read('0_0');
        if(this.stopped)return;
        if(body?.cursor!==undefined && (typeof body.cursor!=='string' || !cursorPattern.test(body.cursor)))
            throw Error('Ungültige Sitzungskennung');
        await this.ingest(body);
        const cursor=body.cursor===undefined ? '' : body.device+':'+body.cursor;
        await this.set('cursor',cursor);
        this.cursor=cursor;
        await this.publish(new Date());
        await this.set('lastError','');
        await this.set('lastSync',Date.now());
        this.lastError='';
        this.nextRead=Date.now()+(cursor ? recoveryInterval : interval);
    }

    notice(marker) {
        if(!markerPattern.test(marker) || marker===this.lastNotice)return;
        this.lastNotice=marker;
        if(marker!==this.cursor)this.requestSync();
    }

    requestSync() {
        if(!this.started || this.stopped)return;
        this.nextRead=0;
        if(!this.pending)this.run();
        else this.syncRequested=true;
    }

    async update() {
        if(!this.device && this.previous) await this.loadDevice(this.previous);
        if(this.stopped)return;
        if(this.device && this.publishedDate!==dateKey(new Date())) await this.publish(new Date());
        if(Date.now()>=this.nextRead)await this.sync();
    }

    run() {
        clearTimeout(this.timer);
        this.pending=this.update().catch(async error=>{
            if(this.stopped)return;
            this.nextRead=Date.now()+retryInterval;
            if(this.lastError!==error.message)this.io.log.warn('Ladearchiv: '+error.message);
            this.lastError=error.message;
            await this.set('lastError',error.message);
        }).catch(error=>this.io.log.warn('Ladearchiv: '+error.message)).finally(()=>{
            this.pending=null;
            if(this.stopped)return;
            if(this.syncRequested){
                this.syncRequested=false;
                this.nextRead=0;
            }
            const delay=Math.min(interval,Math.max(0,this.nextRead-Date.now()));
            this.timer=setTimeout(()=>this.run(),delay);
        });
    }
    async stop() { this.stopped=true; clearTimeout(this.timer); this.abort?.abort(); await this.pending; }
}
module.exports={SessionArchive,validate,key,parseMonth,summarize,aggregate,endpoint};
