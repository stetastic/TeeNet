const fs = require('node:fs');
const vm = require('node:vm');
const assert = require('node:assert/strict');

(async () => {
    const location = { origin: 'http://demo.local', search: '' };
    const window = { location, fetch() { throw Error('Unexpected network call'); } };
    vm.runInNewContext(fs.readFileSync('preview/demo-api.js', 'utf8'), {
        window, location, URL, URLSearchParams, Response, Date, Math, Number, String, Map, JSON,
        document: { addEventListener() {} }
    });
    const read = async path => (await window.fetch(path)).json();
    const data = await read('/api/export.json');
    const firstPage = await read('/api/export.json?after=0_0');
    const latest = firstPage.sessions[0];
    assert.equal(firstPage.cursor, latest.start_epoch+'_'+latest.id);
    assert.equal(firstPage.sessions.length, data.sessions.length);
    const current = await read('/api/export.json?after='+firstPage.cursor);
    assert.equal(current.cursor,firstPage.cursor);
    assert.equal(current.sessions.length,0);
    const previous = firstPage.sessions[1];
    const delta = await read('/api/export.json?after='+previous.start_epoch+'_'+previous.id);
    assert.deepEqual(delta.sessions,[latest]);
    assert.equal((await read('/api/history')).days.length, 30);
    assert.equal(data.sessions.length, (await read('/api/sessions')).retained);
    const lines = (await (await window.fetch('/api/export.lp')).text()).trim().split('\n');
    assert.equal(lines.length, data.sessions.length);
    for (const [i, row] of data.sessions.entries()) {
        assert(Number.isInteger(row.id) && row.energy_kwh >= 1);
        assert(lines[i].startsWith('teenet_session,device='+data.device+',session='+row.id+' '));
        assert(lines[i].endsWith(' '+row.start_epoch+'000000000'));
    }
    const states = new Map(), warnings = [];
    let writes = 0, offline = false, payload = data;
    const ctx = vm.createContext({
        httpGet(url, opts, cb) { cb(offline ? 'offline' : null, {statusCode: 200, data: JSON.stringify(payload)}); },
        existsState: key => states.has(key),
        createStateAsync: async (key, value) => states.set(key, value),
        getStateAsync: async key => ({val: states.get(key)}),
        setStateAsync: async (key, value) => { states.set(key, value); writes++; },
        schedule: () => {}, log: value => warnings.push(value)
    });
    await vm.runInContext(fs.readFileSync('integrations/history/iobroker.js','utf8'), ctx);
    const initial = writes;
    assert(initial > 0);
    await vm.runInContext('archive()', ctx);
    assert.equal(writes, initial);
    offline = true;
    await vm.runInContext('archive()', ctx);
    assert.equal(warnings.length, 1);
    offline = false;
    // Local rollover and reuse of an ID after a reset must not erase external rows.
    const row = {...data.sessions[0], start_epoch: data.sessions[0].start_epoch+1};
    payload = {...data, sessions: [row]};
    await vm.runInContext('archive()', ctx);
    assert.equal(writes, initial+1);
    assert.equal([...states.values()].reduce((n,v)=>n+Object.keys(JSON.parse(v)).length,0),data.sessions.length+1);
    console.log('PASS: 30-day demo, JSON/Influx identities, ioBroker archive, duplicate reads, reset IDs, offline catch-up and local rollover');
})().catch(err=>{console.error(err);process.exitCode=1;});
