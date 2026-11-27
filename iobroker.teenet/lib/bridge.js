'use strict';

const { randomBytes } = require('node:crypto');
const { METRICS, CONTROLS, CONTROL_FEEDBACK, MAX_CONTROL_POWER_KW, config, parseMetric, command, validSource } = require('./protocol');

class Bridge {
    constructor(adapter, mqtt, now = Date.now, archive) {
        this.io = adapter;
        this.mqtt = mqtt;
        this.now = now;
        this.archive = archive;
        this.cache = new Map();
        this.sources = new Map();
        this.units = new Map();
        this.pending = null;
        this.client = null;
        this.closing = false;
        this.broker = false;
        this.available = false;
        this.connected = false;
        this.lastLive = 0;
        this.sequence = 0;
        this.serial = Promise.resolve();
        this.published = new Map();
        this.lastActiveMode = 'manual';
    }

    // ioBroker and MQTT callbacks can overlap. Keep their state mutations ordered.
    enqueue(fn) {
        this.serial = this.serial.then(() => this.closing ? undefined : fn()).catch(error => {
            this.io.log.error(`TeeNet-Adapter: ${error.message}`);
        });
        return this.serial;
    }

    async state(id, val, q = 0) {
        // Voltage-dependent feedback can exceed the command range. Keep the
        // raw value in the feedback cache for command confirmation.
        if (id === 'control.power_kw' && typeof val === 'number') val = Math.min(val, MAX_CONTROL_POWER_KW);
        // Only this adapter writes its read-only outputs. Avoid database reads
        // for unchanged values; writable controls may have external writes.
        const previous = this.published.get(id);
        if (!id.startsWith('control.') && previous && Object.is(previous.val, val) && previous.q === q) return;
        await this.io.setStateChangedAsync(id, { val, ack: true, q });
        this.published.set(id, { val, q });
    }

    async removeLegacyObjects() {
        const states = [
            'info.brokerConnected', 'info.lastSeen',
            'wallbox.target_current_a', 'wallbox.manual_current_a', 'wallbox.manual_power_kw',
            'wallbox.enabled', 'wallbox.mode', 'battery.cloud_buffer', 'battery.assist',
            'inputs.pv_generation_w', 'inputs.battery_soc_pct', 'inputs.battery_charge_w',
            'inputs.battery_discharge_w', 'inputs.car_soc_pct', 'inputs.pv_valid',
            'inputs.battery_soc_valid', 'inputs.battery_power_valid', 'inputs.car_soc_valid',
            'inputs.problem',
        ];
        for (const id of states) {
            if (!await this.io.getObjectAsync(id)) continue;
            await this.io.delStateAsync(id);
            await this.io.delObjectAsync(id);
        }
        if (await this.io.getObjectAsync('inputs')) await this.io.delObjectAsync('inputs');
    }

    async createObjects() {
        for (const [id, name] of Object.entries({ info: 'Verbindung', control: 'Steuerung', wallbox: 'Wallbox', house: 'Hausanschluss',
            battery: 'Hausakku', pv: 'PV', energy: 'Verbrauch' })) {
            await this.io.setObjectNotExistsAsync(id, { type: 'channel', common: { name }, native: {} });
        }
        const info = {
            connection: ['TeeNet erreichbar', 'boolean', 'indicator.connected'],
            commandStatus: ['Befehlsstatus', 'string', 'text'],
            lastError: ['Letzter Befehlsfehler', 'string', 'text'],
        };
        for (const [id, [name, type, role]] of Object.entries(info)) {
            await this.io.setObjectNotExistsAsync(`info.${id}`, { type: 'state', common: { name, type, role, read: true, write: false }, native: {} });
        }
        for (const [topic, [id, name, type, role, unit]] of Object.entries(METRICS)) {
            if (!id) continue;
            await this.io.setObjectNotExistsAsync(id, { type: 'state', common: { name, type, role, read: true, write: false,
                ...(unit ? { unit } : {}) }, native: { topic: `sensor/${topic}` } });
        }
        for (const [key, [name, type, role, unit]] of Object.entries(CONTROLS)) {
            const common = { name, type, role, read: true, write: true, ...(unit ? { unit } : {}) };
            if (key === 'mode') common.states = { manual: 'Manuell', pv: 'PV-Überschuss', off: 'Aus' };
            if (key === 'power_kw') Object.assign(common, { min: 0, max: MAX_CONTROL_POWER_KW, step: 0.5 });
            await this.io.setObjectNotExistsAsync(`control.${key}`, { type: 'state', common, native: {} });
        }
    }

    async start() {
        await this.removeLegacyObjects();
        await this.createObjects();
        // Persisted values have no trustworthy receive time after a restart.
        for (const [id] of Object.values(METRICS)) if (id) await this.state(id, null, 0x02);
        for (const key of Object.keys(CONTROLS)) await this.state(`control.${key}`, null, 0x02);
        await this.state('info.connection', false);
        await this.state('info.commandStatus', 'Bereit');
        await this.state('info.lastError', '');
        // Values saved in writable objects are never replayed at startup.
        await this.io.subscribeStatesAsync('control.*');
        this.cfg = config(this.io.config);
        const ids = this.sourceIds();
        for (const id of ids) {
            if (id.startsWith('teenet.')) throw new Error('Als Quelle einen Original-Datenpunkt wählen, keinen TeeNet-Ausgang.');
            await this.io.subscribeForeignStatesAsync(id);
            const object = await this.io.getForeignObjectAsync(id);
            if (object && (object.type !== 'state' || !['number', 'mixed'].includes(object.common.type))) throw new Error(`Quelle ist kein Zahlen-Datenpunkt: ${id}`);
            const unit = String(object?.common?.unit || '').trim().toLowerCase();
            const isPower = ['pvState', 'batteryPowerState', 'batteryChargeState', 'batteryDischargeState'].some(k => this.cfg[k] === id);
            const isSoc = ['batterySocState', 'carSocState'].some(k => this.cfg[k] === id);
            if (isPower && isSoc) throw new Error(`Quelle kann nicht zugleich Leistung und SOC sein: ${id}`);
            if (isPower && !['', 'w', 'kw'].includes(unit)) throw new Error(`Leistungsquelle ${id}: Einheit muss W oder kW sein.`);
            if (!isPower && !['', '%'].includes(unit)) throw new Error(`SOC-Quelle ${id}: Einheit muss % sein.`);
            this.units.set(id, unit === 'kw' ? 1000 : 1);
            this.sources.set(id, await this.io.getForeignStateAsync(id));
        }
        const namespace = this.io.namespace.replace(/[^a-zA-Z0-9_-]/g, '_');
        this.client = this.mqtt.connect(this.cfg.url, {
            clientId: `iobroker_${namespace}_${randomBytes(4).toString('hex')}`, username: this.cfg.brokerUsername || undefined,
            password: this.cfg.brokerPassword || undefined, protocolVersion: 4, clean: true,
            keepalive: 30, reconnectPeriod: 5000, connectTimeout: 10000,
            queueQoSZero: false, resubscribe: false, rejectUnauthorized: true,
        });
        this.client.on('connect', () => this.enqueue(async () => {
            this.broker = false; this.available = false; this.lastLive = 0;
            await this.updateConnection();
            this.cache.clear();
            this.client.subscribe([`${this.cfg.prefix}/availability`, `${this.cfg.prefix}/sensor/#`], { qos: 0 }, (error, grants) => {
                if (error || !grants || grants.some(g => g.qos === 128)) {
                    this.enqueue(async () => {
                        this.broker = false;
                        await this.state('info.lastError', 'MQTT-Themen konnten nicht abonniert werden. Broker-Berechtigungen prüfen.');
                        await this.updateConnection();
                    });
                } else this.enqueue(async () => {
                    if (!this.client.connected) return;
                    this.broker = true;
                    await this.updateConnection();
                    if (this.subscribed) this.archive?.requestSync();
                    this.subscribed = true;
                });
            });
        }));
        this.client.on('message', (topic, payload, packet) => this.enqueue(() => this.receive(topic, payload, packet)));
        this.client.on('close', () => this.enqueue(async () => {
            this.broker = false; this.available = false; this.lastLive = 0;
            await this.updateConnection();
        }));
        this.client.on('error', () => {
            if (!this.lastWarning || this.now() - this.lastWarning >= 60000) {
                this.lastWarning = this.now();
                this.io.log.warn('MQTT-Verbindung fehlgeschlagen. Broker-Adresse, Anmeldung und Erreichbarkeit prüfen.');
            }
        });
        this.timer = this.io.setInterval(() => this.enqueue(() => this.tick()), 1000);
        await this.forwardInputs(false);
    }

    sourceIds() {
        return [...new Set(['pvState', 'batterySocState', 'carSocState', ...(this.cfg.powerMode === 'signed' ? ['batteryPowerState'] : ['batteryChargeState', 'batteryDischargeState'])]
            .map(k => this.cfg[k]).filter(Boolean))];
    }

    live() { return this.broker && this.available && this.lastLive > 0 && this.now() - this.lastLive <= this.cfg.telemetryTimeout * 1000; }

    freshFeedback(key) {
        const record = this.cache.get(key);
        return record && record.value !== null && !record.stale && this.now() - record.at <= this.cfg.telemetryTimeout * 1000;
    }

    async staleMetric(key, record) {
        record.stale = true;
        const q = record.value === null ? 0x40 : 0x02;
        if (METRICS[key][0]) await this.state(METRICS[key][0], record.value, q);
        for (const [control, feedback] of Object.entries(CONTROL_FEEDBACK)) {
            if (feedback === key && this.pending?.key !== control) await this.state(`control.${control}`, record.value, q);
        }
    }

    async updateConnection() {
        const live = this.live();
        await this.state('info.connection', live);
        if (!live && this.connected) {
            for (const [topic, record] of this.cache) await this.staleMetric(topic, record);
        }
        this.connected = live;
        if (!live && this.pending) await this.finishCommand(false, 'Verbindung unterbrochen; Befehl wird nicht wiederholt.');
    }

    async receive(topic, bytes, packet = {}) {
        if (bytes.length > 512) return;
        if (topic === `${this.cfg.prefix}/availability`) {
            const availability = bytes.toString();
            if (!['online', 'offline'].includes(availability)) return;
            this.available = availability === 'online';
            if (!this.available) this.lastLive = 0;
            await this.updateConnection();
            return;
        }
        if (!topic.startsWith(`${this.cfg.prefix}/sensor/`)) return;
        const key = topic.slice(this.cfg.prefix.length + 8);
        if (key === 'session_archive') {
            this.archive?.notice(bytes.toString());
            return;
        }
        const value = parseMetric(key, bytes);
        if (value === undefined) return;
        // Old retained measurements are not evidence that the ESP is alive.
        if (packet.retain) return;
        const at = this.now();
        this.lastLive = at;
        this.sequence++;
        const q = value === null ? 0x40 : this.broker && this.available ? 0 : 0x02;
        this.cache.set(key, { value, at, seq: this.sequence, stale: q !== 0 });
        if (key === 'mode' && ['manual', 'pv'].includes(value)) this.lastActiveMode = value;
        if (METRICS[key][0]) await this.state(METRICS[key][0], value, q);
        await this.updateConnection();
        for (const [control, feedback] of Object.entries(CONTROL_FEEDBACK)) {
            if (feedback === key && !(this.pending && this.pending.key === control)) {
                await this.state(`control.${control}`, value, q);
            }
        }
        if (this.pending && this.connected && this.pending.expected.every(([field, expected, tolerance = 0.051]) => {
            const actual = this.cache.get(field);
            return actual && actual.seq > this.pending.seq && this.freshFeedback(field) &&
                (typeof expected === 'number' ? Math.abs(actual.value - expected) < tolerance : actual.value === expected);
        })) await this.finishCommand(true);
    }

    async write(id, state) {
        if (this.sources.has(id)) { this.sources.set(id, state); return; }
        if (!id.startsWith(`${this.io.namespace}.control.`) || !state || state.ack) return;
        const key = id.slice(this.io.namespace.length + 9);
        if (!Object.hasOwn(CONTROLS, key)) return;
        try {
            let payload = command(key, state.val), wireKey = key;
            if (this.cfg.readOnly !== false) throw new Error('Steuerung ist deaktiviert. „Nur lesen“ in den Adapter-Einstellungen ausschalten.');
            if (!this.live() || !this.client.connected) throw new Error('Keine aktuelle Rückmeldung vom ESP. Befehl nicht gesendet.');
            const isStop = (key === 'enabled' && state.val === false) || (key === 'mode' && state.val === 'off') || (key === 'power_kw' && state.val === 0);
            if (this.pending && !isStop) throw new Error('Ein Befehl wartet noch auf Rückmeldung.');
            if (isStop && this.pending) await this.finishCommand(false, 'Durch Stopp ersetzt.');
            if (!isStop && ![CONTROL_FEEDBACK[key], 'mode', 'enabled'].every(field => this.freshFeedback(field))) {
                throw new Error('Steuerungsrückmeldungen fehlen oder sind veraltet. Befehl nicht gesendet.');
            }
            let expected = [[CONTROL_FEEDBACK[key], state.val]];
            if (key === 'power_kw') {
                expected = [['manual_power_kw', state.val, 0.1], ['mode', 'manual'], ['enabled', state.val > 0]];
            }
            if (key === 'mode') expected.push(['enabled', state.val !== 'off']);
            // TeeNet ignores enabled=true while mode=off. Restore the last
            // active mode explicitly; after adapter startup the default is manual.
            if (key === 'enabled' && state.val && this.cache.get('mode')?.value === 'off') {
                wireKey = 'mode'; payload = this.lastActiveMode;
                expected.push(['mode', this.lastActiveMode]);
            }
            this.pending = { key, value: state.val, expected, at: this.now(), seq: this.sequence };
            const sent = this.pending;
            await this.state('info.lastError', '');
            await this.state('info.commandStatus', 'Gesendet – Rückmeldung ausstehend');
            // No retain, offline queue or delivery retry: reconnect must not start a car.
            this.client.publish(`${this.cfg.prefix}/command/${wireKey}`, payload, { retain: false, qos: 0 }, error => {
                if (error) this.enqueue(() => this.pending === sent ? this.finishCommand(false, 'MQTT-Befehl konnte nicht gesendet werden.') : undefined);
            });
        } catch (error) {
            await this.state('info.lastError', error.message);
            if (!this.pending) await this.state('info.commandStatus', 'Nicht gesendet');
            const actual = this.cache.get(CONTROL_FEEDBACK[key]);
            // Keep an earlier pending write intact if a user writes again too soon.
            if (this.pending?.key === key) await this.io.setStateAsync(`control.${key}`, { val: this.pending.value, ack: false });
            else await this.state(`control.${key}`, actual?.value ?? null, actual && this.live() ? 0 : 0x02);
        }
    }

    async finishCommand(ok, error = '') {
        if (!this.pending) return;
        const pending = this.pending;
        this.pending = null;
        const actual = this.cache.get(CONTROL_FEEDBACK[pending.key]);
        await this.state(`control.${pending.key}`, ok ? pending.value : actual?.value ?? null, ok ? 0 : 0x02);
        await this.state('info.commandStatus', ok ? 'Rückmeldung bestätigt' : 'Nicht bestätigt');
        await this.state('info.lastError', error);
    }

    async tick() {
        await this.updateConnection();
        if (this.pending && this.now() - this.pending.at >= 45000) {
            await this.finishCommand(false, 'Keine passende Rückmeldung innerhalb von 45 Sekunden. Anschluss, Freigabe und Ladegrenzen prüfen.');
        }
        for (const [key, record] of this.cache) {
            const ttl = key.startsWith('energy/') ? 180000 : this.cfg.telemetryTimeout * 1000;
            if (!record.stale && this.now() - record.at > ttl) {
                await this.staleMetric(key, record);
            }
        }
        if (!this.lastForward || this.now() - this.lastForward >= this.cfg.inputInterval * 1000) {
            this.lastForward = this.now();
            await this.forwardInputs(this.broker && this.client?.connected);
        }
    }

    source(key, maxAge) {
        const id = this.cfg[key];
        const state = this.sources.get(id);
        return validSource(state, maxAge, this.now()) ? state.val * (this.units.get(id) || 1) : null;
    }

    async forwardInputs(send) {
        const issues = [];
        const pub = (key, value) => {
            if (send) this.client.publish(`${this.cfg.prefix}/input/${key}`, String(value), { qos: 0, retain: false });
        };
        const number = async (key, value, valid) => {
            if (valid) pub(key, value);
        };
        const single = async (field, topic, validity, maxAge, max, clampNegative = false) => {
            if (!this.cfg[field]) return;
            let value = this.source(field, maxAge);
            if (value !== null && clampNegative && value >= -100) value = Math.max(0, value);
            const valid = value !== null && value >= 0 && value <= max;
            await number(topic, value, valid);
            if (!valid) { pub(topic.replace(/_(pct|w)$/, '_valid'), false); issues.push(field); }
        };
        await single('pvState', 'pv_generation_w', 'pv_valid', Number(this.cfg.energyMaxAge || 90), 25000);
        await single('batterySocState', 'battery_soc_pct', 'battery_soc_valid', Number(this.cfg.batterySocMaxAge || 900), 100, true);
        await single('carSocState', 'car_soc_pct', 'car_soc_valid', Number(this.cfg.carSocMaxAge || 1800), 100);
        const powerConfigured = this.cfg.powerMode === 'signed' ? this.cfg.batteryPowerState : this.cfg.batteryChargeState || this.cfg.batteryDischargeState;
        if (powerConfigured) {
            let charge, discharge;
            const age = Number(this.cfg.energyMaxAge || 90);
            if (this.cfg.powerMode === 'signed') {
                const raw = this.source('batteryPowerState', age);
                const watts = raw === null ? null : raw * (this.cfg.positive === 'discharge' ? -1 : 1);
                charge = watts === null ? null : Math.max(0, watts);
                discharge = watts === null ? null : Math.max(0, -watts);
            } else {
                charge = this.source('batteryChargeState', age);
                discharge = this.source('batteryDischargeState', age);
            }
            const valid = charge !== null && discharge !== null && charge >= 0 && discharge >= 0 && charge <= 12000 && discharge <= 12000;
            await number('battery_charge_w', charge, valid);
            await number('battery_discharge_w', discharge, valid);
            if (!valid) { pub('battery_power_valid', false); issues.push('Akkuleistung'); }
        }
        const problem = issues.length ? `Quelle fehlt, ist ungültig oder veraltet: ${issues.join(', ')}` : '';
        if (problem !== this.sourceProblem) {
            this.sourceProblem = problem;
            if (problem) this.io.log.warn(problem);
            else this.io.log.info('Ausgewählte TeeNet-Datenquellen sind wieder aktuell.');
        }
    }

    async stop() {
        this.closing = true;
        if (this.timer) this.io.clearInterval(this.timer);
        await this.serial;
        if (this.pending) await this.finishCommand(false, 'Adapter beendet; Befehl wird nicht wiederholt.');
        // Invalidate only the sources this instance was actually forwarding.
        if (this.client?.connected && this.cfg) {
            const topics = [];
            if (this.cfg.pvState) topics.push('pv_generation_valid');
            if (this.cfg.batterySocState) topics.push('battery_soc_valid');
            if (this.cfg.carSocState) topics.push('car_soc_valid');
            if (this.cfg.powerMode === 'signed' ? this.cfg.batteryPowerState : this.cfg.batteryChargeState || this.cfg.batteryDischargeState) topics.push('battery_power_valid');
            for (const topic of topics) this.client.publish(`${this.cfg.prefix}/input/${topic}`, 'false', { qos: 0, retain: false });
            await new Promise(resolve => {
                const timer = setTimeout(() => { this.client.end(true); resolve(); }, 1500);
                this.client.end(false, {}, () => { clearTimeout(timer); resolve(); });
            });
        } else this.client?.end(true);
        await this.state('info.connection', false);
    }
}

module.exports = Bridge;
