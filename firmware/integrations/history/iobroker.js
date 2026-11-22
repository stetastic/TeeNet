// ioBroker JavaScript ab 7.9. Bei Bedarf teenet.local durch die ESP-IP ersetzen.
const url = 'http://teenet.local/api/export.json';
let busy = false;

async function archive() {
    if (busy) return;
    busy = true;
    try {
        const data = await new Promise((resolve, reject) => {
            httpGet(url, { timeout: 10000 }, (err, res) => {
                if (err || res.statusCode !== 200) reject(Error(err || 'HTTP ' + res.statusCode));
                else resolve(res.data);
            });
        });
        const payload = JSON.parse(data);
        if (payload.schema !== 1 || !/^teenet(?:_[0-9a-f]+)?$/.test(payload.device) ||
            !Array.isArray(payload.sessions)) throw Error('Unbekanntes TeeNet-Archivformat');
        const months = new Map();
        for (const row of payload.sessions) {
            const month = row.date ? String(row.date).slice(0, 6) : 'ohneDatum';
            if (!months.has(month)) months.set(month, []);
            months.get(month).push(row);
        }
        for (const [month, rows] of months) {
            const state = '0_userdata.0.TeeNet.Archiv.' + payload.device + '.' + month;
            if (!existsState(state)) {
                await createStateAsync(state, '{}', false, { type: 'string', role: 'json', read: true, write: false });
            }
            const previous = (await getStateAsync(state)).val;
            const entries = JSON.parse(previous);
            if (!entries || typeof entries !== 'object' || Array.isArray(entries)) throw Error('Ungültiges Monatsarchiv ' + month);
            for (const row of rows) entries[row.start_epoch + '_' + row.id] = row;
            const next = JSON.stringify(entries);
            if (next !== previous) await setStateAsync(state, next, true);
        }
    } catch (err) {
        log('TeeNet-Archiv: ' + err.message, 'warn');
    } finally {
        busy = false;
    }
}

schedule('*/5 * * * *', archive);
archive();
