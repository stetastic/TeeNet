'use strict';

const MAX_CONTROL_POWER_KW = 11;

// Wire contract from TeeNet 1.13 main/app.c. No HTTP polling is required.
const METRICS = {
    estimate_w: ['wallbox.power_w', 'Ladeleistung', 'number', 'value.power', 'W', 0, 50000],
    // Feedback used for command confirmation, intentionally not exposed as a
    // second ioBroker state beside control.power_kw.
    manual_power_kw: [null, null, 'number', null, null, 0, 50],
    house_power_w: ['house.power_w', 'Hausanschluss: Bezug (+), Einspeisung (−)', 'number', 'value.power', 'W', -1000000, 1000000],
    meter_ok: ['wallbox.meter_ok', 'Wallbox-Zähler verbunden', 'boolean', 'indicator.reachable'],
    house_meter_ok: ['house.meter_ok', 'Hausanschlusszähler verbunden', 'boolean', 'indicator.reachable'],
    wallbox_ok: ['wallbox.connection', 'Shell-Verbindung', 'boolean', 'indicator.reachable'],
    enabled: [null, null, 'boolean'],
    mode: [null, null, 'string'],
    block_reason: ['wallbox.block_reason', 'Status / Sperrgrund', 'string', 'text'],
    battery_use: [null, null, 'boolean'],
    battery_start_use: [null, null, 'boolean'],
    charge_stop_latched: ['wallbox.locked', 'Ladung gesperrt', 'boolean', 'indicator.alarm'],
    'energy/estimate_total_kwh': ['energy.total_kwh', 'Geladene Energie gesamt', 'number', 'value.energy', 'kWh', 0, 1e12],
    'energy/estimate_today_kwh': ['energy.today_kwh', 'Geladene Energie heute', 'number', 'value.energy', 'kWh', 0, 1e9],
    'energy/estimate_today_cost': ['energy.today_cost', 'Kosten heute (TeeNet-MQTT-Wert)', 'number', 'value', '€', 0, 1e9],
    // Optional TeeNet outputs when Home Assistant publishing is enabled.
    battery_soc_pct: ['battery.soc_pct', 'Hausakku-Ladezustand', 'number', 'value.battery', '%', 0, 100],
    pv_generation_w: ['pv.power_w', 'PV-Erzeugung', 'number', 'value.power', 'W', 0, 25000],
    battery_charge_w: ['battery.charge_w', 'Hausakku lädt', 'number', 'value.power', 'W', 0, 12000],
    battery_discharge_w: ['battery.discharge_w', 'Hausakku entlädt', 'number', 'value.power', 'W', 0, 12000],
};

const CONTROLS = {
    enabled: ['Laden freigeben', 'boolean', 'switch'],
    mode: ['Betriebsart', 'string', 'level.mode'],
    power_kw: ['Ladeleistung (startet Manuellbetrieb)', 'number', 'level', 'kW'],
    battery_use: ['Akku-Wolkenpuffer', 'boolean', 'switch'],
    battery_start_use: ['Auto aus Hausakku laden', 'boolean', 'switch'],
};

const CONTROL_FEEDBACK = {
    enabled: 'enabled', mode: 'mode', power_kw: 'manual_power_kw',
    battery_use: 'battery_use', battery_start_use: 'battery_start_use',
};

function config(raw) {
    const host = String(raw.brokerHost || '').trim();
    if (!host || /[\s/@?#]/.test(host)) throw new Error('MQTT-Broker: Hostname oder IP ohne URL eintragen.');
    const port = Number(raw.brokerPort || (raw.tls ? 8883 : 1883));
    if (!Number.isInteger(port) || port < 1 || port > 65535) throw new Error('Ungültiger MQTT-Port.');
    const prefix = String(raw.topicPrefix || 'wallbox-ems').trim().replace(/\/+$/, '');
    if (!prefix || prefix.startsWith('/') || /[+#\s\u0000]/.test(prefix) || Buffer.byteLength(prefix) > 63) {
        throw new Error('Ungültiges TeeNet Topic-Präfix (maximal 63 Bytes).');
    }
    const telemetryTimeout = Number(raw.telemetryTimeout || 90);
    if (!Number.isInteger(telemetryTimeout) || telemetryTimeout < 45 || telemetryTimeout > 300) throw new Error('Verbindungsfrist: 45–300 Sekunden.');
    const inputInterval = Number(raw.inputInterval || 10);
    if (![5, 10].includes(inputInterval)) throw new Error('Weitergabeintervall: 5 oder 10 Sekunden.');
    const powerMode = raw.batteryPowerMode || 'separate';
    if (!['separate', 'signed'].includes(powerMode)) throw new Error('Ungültige Akkuleistungsquelle.');
    const positive = raw.batteryPositive || 'charge';
    if (!['charge', 'discharge'].includes(positive)) throw new Error('Ungültiges Akkuleistungsvorzeichen.');
    for (const key of ['energyMaxAge', 'batterySocMaxAge', 'carSocMaxAge']) {
        const age = Number(raw[key] || { energyMaxAge: 90, batterySocMaxAge: 900, carSocMaxAge: 1800 }[key]);
        if (!Number.isInteger(age) || age < 10 || age > 86400) throw new Error('Ungültiges maximales Datenalter.');
    }
    for (const key of ['pvState', 'batterySocState', 'batteryChargeState', 'batteryDischargeState', 'batteryPowerState', 'carSocState']) {
        if (raw[key] && (!String(raw[key]).trim() || String(raw[key]).length > 512)) throw new Error('Ungültige Datenpunkt-ID.');
    }
    const bracketed = host.includes(':') && !host.startsWith('[') ? `[${host}]` : host;
    const url = `${raw.tls ? 'mqtts' : 'mqtt'}://${bracketed}:${port}`;
    try { new URL(url); } catch { throw new Error('Ungültige Broker-Adresse.'); }
    const result = { ...raw, host, port, prefix, url, telemetryTimeout, inputInterval, powerMode, positive };
    if (!raw.pvEnabled) result.pvState = '';
    if (!raw.batteryEnabled) for (const key of ['batterySocState', 'batteryChargeState', 'batteryDischargeState', 'batteryPowerState']) result[key] = '';
    if (!raw.carEnabled) result.carSocState = '';
    if (raw.pvEnabled && !result.pvState) throw new Error('Für PV einen Leistungs-Datenpunkt auswählen.');
    if (raw.batteryEnabled && !result.batterySocState) throw new Error('Für den Hausakku einen SOC-Datenpunkt auswählen.');
    if (raw.batteryEnabled && powerMode === 'separate' && Boolean(result.batteryChargeState) !== Boolean(result.batteryDischargeState)) throw new Error('Für Akkuleistung beide Datenpunkte auswählen: Laden und Entladen.');
    if (raw.carEnabled && !result.carSocState) throw new Error('Für das Fahrzeug einen SOC-Datenpunkt auswählen.');
    return result;
}

function parseMetric(key, bytes) {
    if (!Object.hasOwn(METRICS, key)) return undefined;
    const def = METRICS[key];
    if (!def || bytes.length > 512) return undefined;
    const text = bytes.toString().trim();
    if (text === 'null') return null;
    if (def[2] === 'boolean') return text === 'true' || text === '1' ? true : text === 'false' || text === '0' ? false : undefined;
    if (def[2] === 'number') {
        if (!/^-?\d+(?:\.\d+)?(?:[eE][+-]?\d+)?$/.test(text)) return undefined;
        const n = Number(text);
        return Number.isFinite(n) && n >= def[5] && n <= def[6] ? n : undefined;
    }
    if (key === 'mode') return ['off', 'manual', 'pv', 'grid_limit'].includes(text) ? text : undefined;
    return text.length <= 160 ? text : undefined;
}

function command(key, value) {
    if (!CONTROLS[key]) throw new Error('Unbekannter Befehl.');
    if (key === 'mode') {
        if (!['manual', 'pv', 'off'].includes(value)) throw new Error('Betriebsart: manual, pv oder off.');
    } else if (key === 'power_kw') {
        if (typeof value !== 'number' || !Number.isFinite(value) ||
            !(value === 0 || ((value >= 2 && value <= 3.5) || (value >= 6 && value <= MAX_CONTROL_POWER_KW)) && Math.abs(value * 2 - Math.round(value * 2)) < 0.0001)) {
            throw new Error('Leistung: Aus (0), 2–3,5 kW oder 6–11 kW in 0,5-kW-Schritten. Der Anschluss muss den Bereich unterstützen.');
        }
    } else if (typeof value !== 'boolean') throw new Error('Schalter benötigen true oder false.');
    return String(value);
}

function validSource(state, maxAge, now) {
    return state && typeof state.val === 'number' && Number.isFinite(state.val) && state.ack === true &&
        (state.q === undefined || state.q === 0) && Number.isFinite(state.ts) &&
        state.ts <= now + 2000 && now - state.ts <= maxAge * 1000;
}

module.exports = { METRICS, CONTROLS, CONTROL_FEEDBACK, MAX_CONTROL_POWER_KW, config, parseMetric, command, validSource };
