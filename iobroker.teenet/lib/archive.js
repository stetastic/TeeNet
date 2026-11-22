'use strict';

const PREFIX = '0_userdata.0.TeeNet.Archiv.';
const DEVICE = /^teenet(?:_[a-f0-9]+)?$/;
const PERIODS = { today: 'Heute', month: 'Monat', year: 'Jahr', total: 'Seit Aufzeichnung' };
const FIELDS = {
    energy_kwh: ['Ladeenergie', 'kWh'], solar_kwh: ['Solar / Hausakku', 'kWh'], grid_kwh: ['Netz', 'kWh'],
    cost_eur: ['Ladekosten', '€'], solar_cost_eur: ['Kosten Solar / Hausakku', '€'], grid_cost_eur: ['Netzkosten', '€'],
};
const interval = 300000;
const calendar = new Intl.DateTimeFormat('sv-SE',{timeZone:'Europe/Berlin',year:'numeric',month:'2-digit',day:'2-digit'});

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
    constructor(io) { this.io=io; this.months=new Map(); this.stopped=false; }
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
            ['recentSessions','Letzte Ladesitzungen','string','json']]){
            await io.setObjectNotExistsAsync('stats.'+k,stateObject(name,type,role));
        }
        const last=await io.getStateAsync('stats.device');
        if(last && DEVICE.test(last.val)) this.previous=last.val;
        if(!this.stopped) this.run();
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
        const date=calendar.format(now).replaceAll('-','');
        const {periods,recent,timeline}=aggregate(this.months,date);
        for(const [period,sum] of Object.entries(periods)) {
            for(const [k,val] of Object.entries(totals(sum))) await this.set(period+'.'+k,val);
        }
        await this.set('months',JSON.stringify(timeline));
        await this.set('recentSessions',JSON.stringify(recent));
    }
    async poll() {
        if(!this.device && this.previous) await this.loadDevice(this.previous);
        let readError;
        try {
            this.abort=new AbortController();
            const response=await fetch(this.url,{redirect:'error',signal:AbortSignal.any([this.abort.signal,AbortSignal.timeout(10000)])});
            if(!response.ok){await response.body?.cancel();throw Error('HTTP '+response.status);}
            const body=await response.json();
            if(this.stopped)return;
            await this.ingest(body);
        } catch(error){readError=error;}
        if(this.stopped)return;
        if(this.device) await this.publish(new Date());
        if(readError)throw readError;
        await this.set('lastError','');
        await this.set('lastSync',Date.now());
        this.lastError='';
    }
    run() {
        this.pending=this.poll().catch(async error=>{
            if(this.stopped)return;
            if(this.lastError!==error.message)this.io.log.warn('Ladearchiv: '+error.message);
            this.lastError=error.message;
            await this.set('lastError',error.message);
        }).catch(error=>this.io.log.warn('Ladearchiv: '+error.message)).finally(()=>{
            if(!this.stopped)this.timer=setTimeout(()=>this.run(),interval);
        });
    }
    async stop() { this.stopped=true; clearTimeout(this.timer); this.abort?.abort(); await this.pending; }
}
module.exports={SessionArchive,validate,key,parseMonth,summarize,aggregate,endpoint};
