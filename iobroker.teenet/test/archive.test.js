'use strict';
const test=require('node:test');
const assert=require('node:assert/strict');
const {SessionArchive,endpoint}=require('../lib/archive');

function fixture(){
    const states=new Map();
    return {states, log:{warn(){}}, config:{archiveHost:'teenet.local'},setObjectNotExistsAsync:async()=>{},
        setForeignObjectNotExistsAsync:async()=>{},getStateAsync:async k=>states.get(k),
        setStateChangedAsync:async(k,v)=>states.set(k,v),setForeignStateAsync:async(k,v)=>states.set(k,v),
        getForeignStatesAsync:async pattern=>Object.fromEntries([...states].filter(([k])=>k.startsWith(pattern.slice(0,-1)))),
        getForeignStateAsync:async k=>states.get(k)};
}
const solar={id:7,start_epoch:1795343000,end_epoch:1795344000,date:20261122,charging_s:1000,flags:0,
    energy_kwh:5,grid_kwh:2,solar_kwh:3,cost_eur:.94,grid_cost_eur:.7,solar_cost_eur:.24};
const older={...solar,id:4,date:20261030,start_epoch:1793330000};
const packet=(sessions,device='teenet_0123456789ab')=>({schema:1,device,sessions});
test('durable monthly storage survives restarts, repeated polls, corrections and expiry on the ESP',async()=>{
    const io=fixture();const a=new SessionArchive(io);
    await a.ingest(packet([older,solar]));await a.ingest(packet([solar]));
    const b=new SessionArchive(io);await b.ingest(packet([{...solar,cost_eur:1,grid_cost_eur:.76}]));
    await b.publish(new Date('2026-11-22T15:00:00Z'));
    const val=k=>io.states.get('stats.'+k).val;
    assert.equal(val('today.sessions'),1);assert.equal(val('month.solar_kwh'),3);assert.equal(val('year.grid_kwh'),4);
    assert.equal(val('total.cost_eur'),1.94);assert.equal(val('total.grid_cost_eur'),1.46);assert.equal(val('total.solar_pct'),60);
    assert.equal(JSON.parse(val('months')).length,2);assert.equal(JSON.parse(val('recentSessions'))[0].id,7);
    await b.publish(new Date('2026-11-22T23:04:00Z'));assert.equal(val('today.sessions'),0);
    await b.ingest(packet([], 'teenet_aaaa'));await b.publish(new Date());assert.equal(val('total.sessions'),0);
    await b.ingest(packet([]));await b.publish(new Date());assert.equal(val('total.sessions'),2);
});
test('preserves legacy monthly states and rejects corrupt records before overwriting',async()=>{
    const io=fixture(),a=new SessionArchive(io),id='0_userdata.0.TeeNet.Archiv.teenet_0123456789ab.202611';
    io.states.set(id,{val:JSON.stringify({[solar.start_epoch+'_7']:solar})});await a.ingest(packet([solar]));
    const before=io.states.get(id).val;
    await assert.rejects(a.ingest(packet([older,{...solar,solar_kwh:null}])));assert.equal(io.states.get(id).val,before);
    io.states.set(id,{val:'broken'});await assert.rejects(a.ingest(packet([solar])));assert.equal(io.states.get(id).val,'broken');
    assert.equal(endpoint('192.168.1.166').pathname,'/api/export.json');assert.throws(()=>endpoint('a/b'));
});
