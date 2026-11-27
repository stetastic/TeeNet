'use strict';
const test = require('node:test');
const assert = require('node:assert/strict');
const { fixture } = require('./helpers');

test('startup does not replay persisted controls; retained online alone is not a live connection', async () => {
    const f = await fixture({ readOnly: false });
    f.io.states.set('control.power_kw', { val: 11, ack: false });
    await f.start();
    await f.receive('availability', 'online', true);
    await f.receive('sensor/estimate_w', 11000, true);
    assert.equal(f.io.value('info.connection'), false);
    await f.write('enabled', true);
    assert.equal(f.client.sent.length, 0);
    await f.online();
    assert.equal(f.io.value('info.connection'), true);
    assert.equal(f.client.sent.length, 0);
    assert.equal(f.client.options.queueQoSZero, false);
    await f.bridge.stop();
});

test('power control is only confirmed by fresh matching power, mode and enable feedback', async () => {
    const f = await fixture({ readOnly: false }); await f.start(); await f.online();
    await f.write('power_kw', 6);
    assert.deepEqual(f.client.sent.at(-1), { topic: 'wallbox-ems/command/power_kw', payload: '6', retain: false, qos: 0 });
    assert.ok(f.bridge.pending);
    await f.receive('sensor/manual_power_kw', 6);
    assert.ok(f.bridge.pending);
    await f.receive('sensor/enabled', true);
    assert.ok(f.bridge.pending);
    await f.receive('sensor/mode', 'manual');
    assert.equal(f.bridge.pending, null);
    assert.equal(f.io.value('info.commandStatus'), 'Rückmeldung bestätigt');
    assert.equal(f.io.states.get('control.power_kw').ack, true);
    await f.write('power_kw', 6, true);
    assert.equal(f.client.sent.length, 1);
    await f.bridge.stop();
});

test('above-limit feedback stays measurable without overflowing the control or confirming a different setpoint', async () => {
    const f = await fixture({ readOnly: false }); await f.start(); await f.online();
    await f.receive('sensor/manual_power_kw', 11.5);
    await f.receive('sensor/estimate_w', 11500);
    assert.equal(f.io.value('control.power_kw'), 11);
    assert.equal(f.io.value('wallbox.power_w'), 11500);
    assert.equal(f.client.sent.length, 0);
    await f.write('power_kw', 11);
    await f.receive('sensor/manual_power_kw', 11.5);
    await f.receive('sensor/mode', 'manual');
    await f.receive('sensor/enabled', true);
    assert.ok(f.bridge.pending);
    await f.receive('availability', 'offline');
    assert.deepEqual(f.io.states.get('control.power_kw').val, 11);
    assert.equal(f.io.states.get('control.power_kw').q, 0x02);
    await f.receive('sensor/manual_power_kw', 'null');
    assert.equal(f.io.value('control.power_kw'), null);
    await f.bridge.stop();
});

test('read-only mode blocks commands; invalid power and mode are never sent', async () => {
    const f = await fixture(); await f.start(); await f.online();
    await f.write('enabled', true);
    assert.match(f.io.value('info.lastError'), /deaktiviert/);
    f.bridge.cfg.readOnly = false;
    await f.write('power_kw', 4); await f.write('power_kw', 5.5); await f.write('mode', 'invalid');
    assert.equal(f.client.sent.length, 0);
    await f.bridge.stop();
});

test('unconfirmed or disconnected commands are failed, never queued or retried', async () => {
    const f = await fixture({ readOnly: false }); await f.start(); await f.online();
    await f.write('enabled', true);
    f.advance(45000); await f.bridge.tick();
    assert.equal(f.bridge.pending, null);
    assert.equal(f.io.value('info.commandStatus'), 'Nicht bestätigt');
    await f.online(); await f.write('mode', 'pv');
    f.client.connected = false; f.client.emit('close'); await f.bridge.serial;
    assert.equal(f.io.value('info.connection'), false); assert.equal(f.bridge.pending, null);
    f.client.connected = true; f.client.emit('connect'); await f.bridge.serial; await f.online();
    assert.equal(f.client.sent.length, 2);
    await f.bridge.stop();
});

test('concurrent writes preserve the first pending command and reject additional writes', async () => {
    const f = await fixture({ readOnly: false }); await f.start(); await f.online();
    await f.write('power_kw', 6); await f.write('power_kw', 11); await f.write('mode', 'pv');
    assert.equal(f.client.sent.length, 1); assert.equal(f.bridge.pending.value, 6);
    assert.equal(f.io.value('control.power_kw'), 6);
    await f.bridge.stop();
});

test('telemetry becomes stale; missing metrics and null readings are not replaced by zero', async () => {
    const f = await fixture(); await f.start(); await f.online();
    await f.receive('sensor/estimate_w', 2300);
    await f.receive('sensor/house_power_w', 'null');
    assert.equal(f.io.value('house.power_w'), null); assert.equal(f.io.states.get('house.power_w').q, 0x40);
    f.advance(91000); await f.bridge.tick();
    assert.equal(f.io.value('info.connection'), false);
    assert.equal(f.io.value('wallbox.power_w'), 2300); assert.equal(f.io.states.get('wallbox.power_w').q, 0x02);
    await f.receive('sensor/estimate_w', 0);
    assert.equal(f.io.states.get('wallbox.power_w').q, 0);
    await f.bridge.stop();
});

test('source selection converts kW to W, splits signed battery power and clamps negative battery SOC', async () => {
    const f = await fixture({ pvEnabled: true, pvState: 'solar.power', batteryEnabled: true, batterySocState: 'akku.soc',
        batteryPowerMode: 'signed', batteryPowerState: 'akku.power', batteryPositive: 'discharge' });
    for (const [id, val, unit] of [['solar.power', 8.5, 'kW'], ['akku.soc', -2, '%'], ['akku.power', 4, 'kW']]) {
        f.io.foreign.set(id, { val, ack: true, q: 0, ts: f.now() });
        f.io.foreignObjects.set(id, { type: 'state', common: { type: 'number', unit } });
    }
    await f.start(); await f.bridge.forwardInputs(true);
    const sent = Object.fromEntries(f.client.sent.filter(x => x.topic.includes('/input/')).map(x => [x.topic.split('/').at(-1), x.payload]));
    assert.equal(sent.pv_generation_w, '8500');
    assert.equal(sent.battery_soc_pct, '0');
    assert.equal(sent.battery_charge_w, '0');
    assert.equal(sent.battery_discharge_w, '4000');
    assert.ok(f.client.sent.every(x => x.retain === false && x.qos === 0));
    await f.bridge.stop();
    assert.ok(f.client.sent.some(x => x.topic.endsWith('/input/battery_power_valid') && x.payload === 'false'));
});

test('numeric values from legacy mixed ioBroker states remain usable', async () => {
    const f = await fixture({ pvEnabled: true, pvState: 'javascript.0.PV Gesamt' });
    f.io.foreign.set('javascript.0.PV Gesamt', { val: 7123, ack: true, q: 0, ts: f.now() });
    f.io.foreignObjects.set('javascript.0.PV Gesamt', { type: 'state', common: { type: 'mixed' } });
    await f.start(); await f.bridge.forwardInputs(true);
    assert.ok(f.client.sent.some(x => x.topic.endsWith('/input/pv_generation_w') && x.payload === '7123'));
    await f.bridge.stop();
});

test('stale, deleted and unacknowledged sources invalidate ESP inputs instead of sending saved readings', async () => {
    const f = await fixture({ pvEnabled: true, pvState: 'solar.power', energyMaxAge: 10, carEnabled: true, carSocState: 'car.soc' });
    f.io.foreign.set('solar.power', { val: 8000, ack: true, q: 0, ts: f.now() });
    f.io.foreign.set('car.soc', { val: 50, ack: false, q: 0, ts: f.now() });
    await f.start(); await f.bridge.forwardInputs(true);
    assert.ok(f.client.sent.some(x => x.topic.endsWith('car_soc_valid') && x.payload === 'false'));
    f.advance(11000); f.client.sent.length = 0; await f.bridge.forwardInputs(true);
    assert.ok(f.client.sent.some(x => x.topic.endsWith('pv_generation_valid') && x.payload === 'false'));
    assert.ok(!f.client.sent.some(x => x.topic.endsWith('pv_generation_w')));
    await f.bridge.write('solar.power', null);
    assert.equal(f.bridge.sources.get('solar.power'), null);
    await f.bridge.stop();
});

test('disabled source features do not subscribe, forward or invalidate unrelated publishers', async () => {
    const f = await fixture({ pvState: 'solar.power', batterySocState: 'akku.soc', carSocState: 'car.soc' });
    await f.start(); await f.bridge.forwardInputs(true); await f.bridge.stop();
    assert.deepEqual(f.io.subscriptions, ['control.*']); assert.equal(f.client.sent.length, 0);
});

test('self-referential inputs and incorrect source units fail configuration', async () => {
    const f = await fixture({ pvEnabled: true, pvState: 'teenet.0.wallbox.power_w' });
    await assert.rejects(f.start(), /Original-Datenpunkt/);
    const g = await fixture({ pvEnabled: true, pvState: 'solar.energy' });
    g.io.foreignObjects.set('solar.energy', { type: 'state', common: { type: 'number', unit: 'kWh' } });
    await assert.rejects(g.start(), /Einheit/);
});
