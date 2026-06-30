// CGO600 Bridge — UI app.
// Vanilla DOM rewrite of the design prototype (test-t/project/*).

(function () {
'use strict';

// ─── Timing constants (ms) ───────────────────────────────
const MINUTE_MS = 60000;
const DAY_MS = 86400000;
const TOAST_MS = 2800;          // toast auto-dismiss
const SAVED_FLASH_MS = 1400;    // "Saved" confirmation flash
const WS_RETRY_MIN_MS = 1000;   // WebSocket reconnect backoff floor
const WS_RETRY_MAX_MS = 5000;   // WebSocket reconnect backoff ceiling
const STATE_POLL_MS = 5000;     // fallback /api/state poll when WS is down
const RIDES_REFRESH_MS = 30000; // periodic ride-list refresh

// ─── Tiny DOM helpers ────────────────────────────────────
function el(tag, attrs, children) {
    const n = document.createElement(tag);
    if (attrs) {
        for (const k in attrs) {
            if (k === 'class')    n.className = attrs[k];
            else if (k === 'html') n.innerHTML = attrs[k];
            else if (k === 'text') n.textContent = attrs[k];
            else if (k.indexOf('on') === 0 && typeof attrs[k] === 'function') {
                n.addEventListener(k.slice(2).toLowerCase(), attrs[k]);
            } else if (k === 'style' && typeof attrs[k] === 'object') {
                Object.assign(n.style, attrs[k]);
            } else if (attrs[k] === false || attrs[k] === null || attrs[k] === undefined) {
                // skip
            } else {
                n.setAttribute(k, attrs[k]);
            }
        }
    }
    if (children) appendChildren(n, children);
    return n;
}
function appendChildren(parent, children) {
    if (children === null || children === undefined || children === false) return;
    if (Array.isArray(children)) {
        for (const c of children) appendChildren(parent, c);
        return;
    }
    if (typeof children === 'string' || typeof children === 'number') {
        parent.appendChild(document.createTextNode(String(children)));
        return;
    }
    parent.appendChild(children);
}
function icon(id, opts) {
    opts = opts || {};
    const svg = document.createElementNS('http://www.w3.org/2000/svg', 'svg');
    svg.setAttribute('class', 'icon' + (opts.bold ? ' icon-bold' : '') + (opts.cls ? ' ' + opts.cls : ''));
    svg.setAttribute('viewBox', '0 0 24 24');
    if (opts.size) svg.style.fontSize = opts.size + 'px';
    const use = document.createElementNS('http://www.w3.org/2000/svg', 'use');
    use.setAttributeNS('http://www.w3.org/1999/xlink', 'href', '#' + id);
    use.setAttribute('href', '#' + id);
    svg.appendChild(use);
    return svg;
}
function clear(n) { while (n.firstChild) n.removeChild(n.firstChild); }

// ─── Formatters ──────────────────────────────────────────
const fmt = {
    km(n) { return (Number(n) || 0).toFixed(1); },
    kmInt(n) { return Math.round(Number(n) || 0).toString(); },
    hUnit(locale) { return locale === 'nl' ? 'u' : 'h'; },
    durationMin(min, locale) {
        min = Math.max(0, Math.round(min));
        const h = Math.floor(min / 60), m = min % 60;
        if (h === 0) return m + 'm';
        return h + fmt.hUnit(locale) + ' ' + m + 'm';
    },
    durationLong(min, locale) {
        min = Math.max(0, Math.round(min));
        const h = Math.floor(min / 60), m = min % 60;
        if (h === 0) return m + 'm';
        return h + fmt.hUnit(locale) + ' ' + m + 'm';
    },
    saddleTime(min) {
        min = Math.max(0, Math.round(min));
        return { h: Math.floor(min / 60), m: min % 60 };
    },
    relative(ts, locale) {
        if (!ts) return '';
        const diff = Date.now() - (typeof ts === 'number' ? ts : new Date(ts).getTime());
        const min = Math.round(diff / MINUTE_MS);
        const T = window.bridgeI18n;
        if (isNaN(min)) return '';
        if (min < 1) return T.t('just_now');
        if (min < 60) return T.t('minutes_ago', min);
        const h = Math.round(min / 60);
        if (h < 24) return T.t('hours_ago', h);
        const d = Math.round(h / 24);
        return T.t('days_ago', d);
    },
    timeOnly(ts) {
        const d = new Date(ts);
        if (isNaN(d.getTime())) return '';
        return d.toTimeString().slice(0, 5);
    },
    dateTime(ts, locale) {
        const d = new Date(ts);
        if (isNaN(d.getTime())) return '';
        const time = d.toTimeString().slice(0, 5);
        if (locale === 'nl') {
            const pad = n => n < 10 ? '0' + n : '' + n;
            return pad(d.getDate()) + '-' + pad(d.getMonth() + 1) + '-' + d.getFullYear() + ', ' + time;
        }
        return d.toLocaleString('en', { month: 'short' }) + ' ' + d.getDate() + ', ' + time;
    },
    dateLong(ts, locale) {
        const d = new Date(ts);
        if (isNaN(d.getTime())) return '';
        return d.toLocaleString(locale === 'nl' ? 'nl-NL' : 'en-US', { weekday: 'short', day: 'numeric', month: 'short' });
    },
};

// ─── Components ──────────────────────────────────────────
function statusDot(tone, pulse) {
    const color = tone === 'success' ? 'var(--success)' :
                  tone === 'warning' ? 'var(--warning)' :
                  tone === 'danger'  ? 'var(--danger)'  : 'var(--text-dim)';
    return el('span', { class: 'status-dot' + (pulse ? ' pulse' : ''), style: { color: color } });
}
function chip(text, opts) {
    opts = opts || {};
    const tone = opts.tone || 'neutral';
    return el('span', {
        class: 'chip' + (opts.size === 'sm' ? ' is-sm' : '') + ' tone-' + tone,
    }, [opts.icon || null, text]);
}
function field(label, hint, control) {
    return el('div', { class: 'field' }, [
        el('div', { class: 'field-label', text: label }),
        hint ? el('div', { class: 'field-hint', text: hint }) : null,
        el('div', { class: 'field-control' }, control),
    ]);
}
function textInput(opts) {
    opts = opts || {};
    const input = el('input', {
        type: opts.type || 'text',
        value: opts.value !== undefined && opts.value !== null ? opts.value : '',
        placeholder: opts.placeholder || '',
        autocomplete: opts.autocomplete || 'off',
    });
    if (opts.onChange) input.addEventListener('input', () => opts.onChange(input.value));
    const wrap = el('div', { class: 'text-input' + (opts.mono ? ' mono' : '') }, [input]);
    if (opts.suffix) wrap.appendChild(el('span', { class: 'suffix', text: opts.suffix }));
    return wrap;
}
function numberInput(opts) {
    opts = opts || {};
    const min = opts.min !== undefined ? opts.min : null;
    const max = opts.max !== undefined ? opts.max : null;
    const step = opts.step || 1;
    const inp = el('input', {
        type: 'number',
        value: String(opts.value !== undefined && opts.value !== null ? opts.value : ''),
        min: min !== null ? min : false,
        max: max !== null ? max : false,
        step: step,
    });
    function clamp(v) {
        let x = Number(v);
        if (isNaN(x)) x = min !== null ? min : 0;
        if (min !== null) x = Math.max(min, x);
        if (max !== null) x = Math.min(max, x);
        return x;
    }
    inp.addEventListener('change', () => {
        const v = clamp(inp.value);
        inp.value = v;
        if (opts.onChange) opts.onChange(v);
    });
    const dec = el('button', { type: 'button', text: '−', onclick: () => { inp.value = clamp(Number(inp.value || 0) - step); inp.dispatchEvent(new Event('change')); } });
    const inc = el('button', { type: 'button', text: '+', onclick: () => { inp.value = clamp(Number(inp.value || 0) + step); inp.dispatchEvent(new Event('change')); } });
    const wrap = el('div', { class: 'number-input' }, [inp]);
    if (opts.suffix) wrap.appendChild(el('span', { class: 'suffix', text: opts.suffix }));
    wrap.appendChild(el('div', { class: 'num-step' }, [inc, dec]));
    return wrap;
}
function selectInput(opts) {
    opts = opts || {};
    const sel = el('select');
    (opts.options || []).forEach(o => {
        const op = el('option', { value: o.value, text: o.label });
        if (String(o.value) === String(opts.value)) op.selected = true;
        sel.appendChild(op);
    });
    sel.addEventListener('change', () => opts.onChange && opts.onChange(sel.value));
    return el('div', { class: 'select-input' }, [sel]);
}
function segmented(opts) {
    opts = opts || {};
    const wrap = el('div', { class: 'segmented' + (opts.size === 'sm' ? ' sz-sm' : '') });
    (opts.options || []).forEach(o => {
        const b = el('button', {
            type: 'button', class: o.value === opts.value ? 'is-active' : '',
            onclick: () => opts.onChange && opts.onChange(o.value),
        }, [
            o.icon ? icon(o.icon, { size: 15, bold: true }) : null,
            el('span', { text: o.label }),
        ]);
        wrap.appendChild(b);
    });
    return wrap;
}
function toggle(opts) {
    const b = el('button', {
        type: 'button',
        class: 'toggle' + (opts.value ? ' is-on' : ''),
        'aria-pressed': opts.value ? 'true' : 'false',
        onclick: () => opts.onChange && opts.onChange(!opts.value),
    });
    return b;
}
function bar(value, opts) {
    opts = opts || {};
    const fill = el('div', { class: 'bar-fill', style: { width: Math.max(0, Math.min(100, value)) + '%' } });
    if (opts.color) fill.style.background = opts.color;
    return el('div', { class: 'bar' + (opts.thin ? ' is-thin' : '') + (opts.thick ? ' is-thick' : '') }, [fill]);
}
function miniBars(data) {
    const max = Math.max(1, ...data);
    return el('div', { class: 'mini-bars' }, data.map(v => {
        const h = (v / max) * 100;
        return el('div', {
            class: 'mb-bar' + (v > 0 ? ' is-data' : ''),
            style: { height: h + '%', opacity: v > 0 ? (0.4 + 0.6 * (v / max)) : 1 },
        });
    }));
}
function button(label, opts) {
    opts = opts || {};
    const cls = ['btn'];
    if (opts.variant === 'secondary') cls.push('btn-secondary');
    else if (opts.variant === 'ghost') cls.push('btn-ghost');
    if (opts.danger) { cls.length = 1; cls.push('btn'); cls.push('btn-danger'); }
    if (opts.size === 'sm') cls.push('sz-sm');
    if (opts.size === 'lg') cls.push('sz-lg');
    if (opts.full) cls.push('full');
    const b = el('button', { type: 'button', class: cls.join(' ') });
    if (opts.icon) b.appendChild(opts.icon);
    b.appendChild(el('span', { text: label }));
    if (opts.onClick) b.addEventListener('click', opts.onClick);
    if (opts.disabled) b.disabled = true;
    return b;
}
function sectionLabel(text, iconId) {
    return el('div', { class: 'section-label' }, [
        iconId ? icon(iconId) : null,
        el('span', { text: text }),
    ]);
}
function card(opts, children) {
    opts = opts || {};
    const cls = ['card'];
    if (opts.flush) cls.push('is-flush');
    if (opts.clickable) cls.push('is-clickable');
    if (opts.accent) cls.push('is-accent');
    const c = el('div', { class: cls.join(' ') }, children);
    if (opts.onClick) {
        c.addEventListener('click', opts.onClick);
        c.setAttribute('role', 'button');
    }
    return c;
}
function cardHeader(text, iconId) {
    return el('div', { class: 'card-header' }, [iconId ? icon(iconId) : null, el('span', { text: text })]);
}
function listRow(opts, children) {
    opts = opts || {};
    const cls = ['list-row'];
    if (opts.last) cls.push('last');
    if (opts.onClick) cls.push('is-clickable');
    const r = el('div', { class: cls.join(' ') }, children);
    if (opts.onClick) r.addEventListener('click', opts.onClick);
    return r;
}

// ─── Screen header ───────────────────────────────────────
function screenHeader(title, subtitle, right) {
    const block = el('div', { class: 'screen-title-block' }, [
        el('div', { class: 'screen-title', text: title }),
        subtitle ? el('div', { class: 'screen-subtitle' }, subtitle) : null,
    ]);
    return el('div', { class: 'screen-header' }, [
        el('div', { class: 'screen-header-row' }, [block, right || null]),
    ]);
}

// ─── State + API ─────────────────────────────────────────
const State = {
    bike: null,            // /api/state snapshot
    config: null,          // /api/config
    rides: [],             // /api/rides
};

const Bus = (() => {
    const subs = {};
    return {
        on(ev, fn) { (subs[ev] = subs[ev] || []).push(fn); },
        emit(ev, payload) { (subs[ev] || []).forEach(fn => { try { fn(payload); } catch (e) {} }); },
    };
})();

function api(path, opts) {
    return fetch(path, opts || {}).then(r => {
        if (!r.ok && opts && opts.expectOk) throw new Error('HTTP ' + r.status);
        return r.json().catch(() => ({}));
    });
}
function apiSendJson(path, method, body) {
    return fetch(path, {
        method: method || 'POST',
        headers: { 'Content-Type': 'application/json' },
        body: JSON.stringify(body),
    });
}

// ─── Toast ───────────────────────────────────────────────
let toastTimer = null;
function toast(msg, tone) {
    const t = document.getElementById('toast');
    if (!t) return;
    t.textContent = msg;
    t.className = 'toast tone-' + (tone || 'info') + ' is-visible';
    clearTimeout(toastTimer);
    toastTimer = setTimeout(() => { t.className = 'toast'; }, TOAST_MS);
}

// ─── Status mapping ──────────────────────────────────────
function bridgeStatus() {
    const s = State.bike && State.bike.ble_status;
    if (s === 'connected') return 'connected';
    if (s === 'offline') return 'offline';
    return 'searching';
}
function statusTone(s) {
    return s === 'connected' ? 'success' : s === 'searching' ? 'warning' : 'danger';
}

// ─── Aggregation ─────────────────────────────────────────
function rideStart(r) {
    // window_start is the departure moment recorded by the firmware (last time
    // the bike was seen before it dropped out of BLE range). Use it directly —
    // do NOT estimate from window_end − duration.
    const start = new Date(r.window_start).getTime();
    if (!isNaN(start)) return start;
    return new Date(r.window_end).getTime();
}
function rideEnd(r) { return new Date(r.window_end).getTime(); }
function normalizeRide(r, idx) {
    return {
        id: r.id || ('ride-' + idx),
        raw: r,
        start: rideStart(r),
        end: rideEnd(r),
        km: Number(r.distance_km) || 0,
        durationMin: Math.round((Number(r.ride_duration_sec) || 0) / 60),
        avgKmh: r.avg_speed_kmh != null ? Number(r.avg_speed_kmh) : null,
        maxKmh: r.max_speed_kmh != null ? Number(r.max_speed_kmh) : null,
        batteryFrom: r.battery_start != null ? Number(r.battery_start) : null,
        batteryTo: r.battery_end != null ? Number(r.battery_end) : null,
    };
}
function lifetime(rides) {
    let km = 0, minutes = 0;
    for (const r of rides) { km += r.km; minutes += r.durationMin; }
    return { km: +km.toFixed(1), minutes: minutes, count: rides.length };
}
function aggregateRides(rides, period) {
    const now = Date.now(), DAY = DAY_MS;
    let cutoff = 0;
    if (period === 'week') cutoff = now - 7 * DAY;
    else if (period === 'month') cutoff = now - 30 * DAY;
    const within = rides.filter(r => r.end >= cutoff);
    const km = within.reduce((a, r) => a + r.km, 0);
    const minutes = within.reduce((a, r) => a + r.durationMin, 0);
    return {
        rides: within,
        count: within.length,
        km: +km.toFixed(1),
        minutes: minutes,
        avgKm: within.length ? +(km / within.length).toFixed(1) : 0,
    };
}
function trendBuckets(rides, period) {
    const now = Date.now(), DAY = DAY_MS;
    let bucketMs, n;
    if (period === 'week') { bucketMs = DAY; n = 7; }
    else if (period === 'month') { bucketMs = DAY; n = 30; }
    else { bucketMs = 30 * DAY; n = 12; }
    const buckets = new Array(n).fill(0);
    for (const r of rides) {
        const ageMs = now - r.end;
        const idx = n - 1 - Math.floor(ageMs / bucketMs);
        if (idx >= 0 && idx < n) buckets[idx] += r.km;
    }
    return buckets.map(v => +v.toFixed(1));
}
function groupByDay(rides, locale) {
    const groups = [];
    const byKey = {};
    for (const r of rides) {
        const d = new Date(r.end);
        const key = d.getFullYear() + '-' + d.getMonth() + '-' + d.getDate();
        if (!byKey[key]) {
            const today = new Date();
            const yest = new Date(today.getTime() - DAY_MS);
            let label;
            if (today.toDateString() === d.toDateString()) label = locale === 'nl' ? 'Vandaag' : 'Today';
            else if (yest.toDateString() === d.toDateString()) label = locale === 'nl' ? 'Gisteren' : 'Yesterday';
            else label = d.toLocaleString(locale === 'nl' ? 'nl-NL' : 'en-US', { weekday: 'long', month: 'short', day: 'numeric' });
            const g = { label: label, rides: [] };
            byKey[key] = g;
            groups.push(g);
        }
        byKey[key].rides.push(r);
    }
    return groups;
}

// ─── Routing ─────────────────────────────────────────────
const Route = {
    tab: 'dashboard',
    rideId: null,
};

function setTab(t) { Route.tab = t; Route.rideId = null; render(); }
function openRide(id) { Route.rideId = id; render(); }
function closeRide() { Route.rideId = null; render(); }

// ─── Dashboard ───────────────────────────────────────────
function renderDashboard() {
    const T = window.bridgeI18n;
    const bike = State.bike || {};
    const status = bridgeStatus();
    const tone = statusTone(status);

    const ridesN = State.rides.map(normalizeRide);
    const life = lifetime(ridesN);
    const lastRide = ridesN[0] || null;

    const batteryPct = bike.battery != null ? Number(bike.battery) : null;
    const rangeLeft = bike.remaining_range != null ? Number(bike.remaining_range) : null;
    const lightOn = bike.light === 'on';
    const lightDisabled = status !== 'connected' || bike.light == null;

    const lifeT = fmt.saddleTime(life.minutes);
    // Total distance is the bike's own odometer (total_range_km → `range`),
    // not a sum of logged rides. Falls back to the ride-log sum when the bike
    // hasn't reported yet (e.g. just after boot, still searching).
    const totalKm = bike.range != null ? Number(bike.range) : life.km;

    const screen = el('div', { class: 'screen' }, [
        screenHeader(T.t('tab_dashboard'), el('span', null, [icon('i-bluetooth', { size: 13 }), ' ' + (State.config && State.config.device_name || 'CGO600')])),
        el('div', { class: 'screen-body' }, [
            renderHero(bike, batteryPct, rangeLeft, status, tone, lightOn, lightDisabled),
            renderLifetimeCard(totalKm, life, lifeT, T),
            renderLastRideCard(lastRide, T),
            el('button', { class: 'link-cta', onclick: () => setTab('rides') }, [
                el('span', { text: T.t('view_all_rides') }),
                icon('i-chev-r', { size: 14 }),
            ]),
        ]),
    ]);
    return screen;
}

function renderHero(bike, batteryPct, rangeLeft, status, tone, lightOn, lightDisabled) {
    const T = window.bridgeI18n;

    const batteryColor = batteryPct == null ? 'var(--text-dim)'
        : batteryPct > 50 ? 'var(--success)'
        : batteryPct > 20 ? 'var(--warning)' : 'var(--danger)';

    const hero = el('div', { class: 'card hero-card' }, [
        el('div', { class: 'hero-glow' }),
        el('div', { class: 'hero-row-top' }, [
            el('span', { class: 'status-chip tone-' + tone }, [
                statusDot(tone, status === 'searching'),
                el('span', { text: T.t('status_' + status) }),
            ]),
            el('span', { class: 'hero-last' }, bike.last_connected ? [
                icon('i-clock', { size: 13 }),
                el('span', { class: 'val', text: fmt.relative(bike.last_connected) }),
            ] : [
                icon('i-clock', { size: 13 }),
                el('span', { class: 'val', text: '—' }),
            ]),
        ]),
        el('div', { class: 'hero-row-mid' }, [
            el('div', { class: 'hero-bal' }, [
                el('div', null, [
                    el('div', { class: 'hero-eyebrow', text: T.t('battery') }),
                    el('div', { class: 'hero-battery' }, [
                        batteryPct == null ? '—' : String(Math.round(batteryPct)),
                        el('span', { class: 'unit', text: '%' }),
                    ]),
                ]),
                el('div', { class: 'hero-range-block' }, [
                    el('div', { class: 'hero-eyebrow', text: T.t('range') }),
                    el('div', { class: 'hero-range' }, [
                        rangeLeft == null ? '—' : String(Math.round(rangeLeft)),
                        el('span', { class: 'unit', text: 'km' }),
                    ]),
                ]),
            ]),
            bar(batteryPct == null ? 0 : batteryPct, { color: batteryColor, thin: true }),
            el('div', { class: 'hero-hint', text: T.t('status_' + status + '_hint') }),
        ]),
        renderLightRow(lightOn, lightDisabled),
    ]);
    return hero;
}

function renderLightRow(lightOn, lightDisabled) {
    const T = window.bridgeI18n;
    const cls = ['light-row'];
    if (lightOn) cls.push('is-on');
    if (lightDisabled) cls.push('is-disabled');
    return el('div', { class: cls.join(' ') }, [
        el('div', { class: 'light-row-left' }, [
            el('div', { class: 'light-icon' }, [icon('i-bolt', { size: 18 })]),
            el('div', null, [
                el('div', { class: 'light-title', text: T.t('light') }),
                el('div', { class: 'light-state', text: lightDisabled ? T.t('light_only_connected') : (lightOn ? T.t('light_on') : T.t('light_off')) }),
            ]),
        ]),
        toggle({
            value: lightOn,
            onChange: lightDisabled ? null : () => {
                const next = !lightOn;
                apiSendJson('/api/light', 'POST', { on: next })
                    .then(r => { if (!r.ok) throw new Error(); })
                    .catch(() => toast(T.t('save_failed'), 'error'));
            },
        }),
    ]);
}

function renderLifetimeCard(totalKm, life, lifeT, T) {
    return card({}, [
        cardHeader(T.t('lifetime'), 'i-route'),
        el('div', { class: 'kv-grid cols-3' }, [
            el('div', { class: 'kv-block' }, [
                el('div', { class: 'kv-label-tiny', text: T.t('total_distance') }),
                el('div', { class: 'kv-value sz-30' }, [
                    (Number(totalKm) || 0).toFixed(1),
                    el('span', { class: 'unit', text: 'km' }),
                ]),
            ]),
            el('div', { class: 'kv-block with-divider' }, [
                el('div', { class: 'kv-label-tiny', text: T.t('total_time') }),
                el('div', { class: 'kv-time' }, [
                    el('span', { class: 'num', text: lifeT.h }),
                    el('span', { class: 'unit', text: fmt.hUnit(T.language) }),
                    el('span', { class: 'num', text: lifeT.m }),
                    el('span', { class: 'unit', text: 'm' }),
                ]),
            ]),
            el('div', { class: 'kv-block with-divider' }, [
                el('div', { class: 'kv-label-tiny', text: T.t('total_rides') }),
                el('div', { class: 'kv-value sz-24', text: String(life.count) }),
            ]),
        ]),
    ]);
}

function renderLastRideCard(lastRide, T) {
    if (!lastRide) {
        return card({}, [
            cardHeader(T.t('last_ride'), 'i-flash'),
            el('div', { style: { color: 'var(--text)', fontSize: '16px', fontWeight: '600', marginBottom: '4px' }, text: T.t('no_last_ride') }),
            el('div', { style: { color: 'var(--text-dim)', fontSize: '13.5px', lineHeight: '1.45' }, text: T.t('no_last_ride_hint') }),
        ]);
    }
    const usedBat = (lastRide.batteryFrom != null && lastRide.batteryTo != null)
        ? Math.max(0, lastRide.batteryFrom - lastRide.batteryTo) : null;
    return card({ clickable: true, onClick: () => openRide(lastRide.id) }, [
        el('div', { class: 'last-ride-head' }, [
            cardHeader(T.t('last_ride'), 'i-flash'),
            el('span', { class: 'last-ride-meta', text: fmt.relative(lastRide.end) }),
        ]),
        el('div', { class: 'last-ride-row' }, [
            el('div', { class: 'last-ride-km' }, [
                lastRide.km.toFixed(1),
                el('span', { class: 'unit', text: 'km' }),
            ]),
            el('div', { class: 'flex-grow' }),
            chip(null, { size: 'sm', icon: icon('i-chev-r', { size: 13 }) }),
        ]),
        el('div', { class: 'kv-grid cols-3-strict last-ride-divider' }, [
            miniStat(T.t('duration'), fmt.durationMin(lastRide.durationMin, T.language), null),
            miniStat(T.t('avg_speed'), lastRide.avgKmh != null ? lastRide.avgKmh.toFixed(1) : '—', 'km/h'),
            miniStat(T.t('battery_used'), usedBat != null ? '-' + usedBat : '—', '%', 'warning'),
        ]),
    ]);
}

function miniStat(label, value, unit, tone) {
    return el('div', null, [
        el('div', { class: 'mini-stat-label', text: label }),
        el('div', { class: 'mini-stat-value' + (tone ? ' tone-' + tone : '') }, [
            String(value),
            unit ? el('span', { class: 'unit', text: unit }) : null,
        ]),
    ]);
}

// ─── Rides ───────────────────────────────────────────────
const RidesView = {
    period: 'week',
};
function renderRides() {
    const T = window.bridgeI18n;
    const locale = T.language;
    const ridesN = State.rides.map(normalizeRide);
    const agg = aggregateRides(ridesN, RidesView.period);
    const buckets = trendBuckets(ridesN, RidesView.period);
    const trendLabel = RidesView.period === 'week' ? T.t('trend_label')
        : RidesView.period === 'month' ? T.t('trend_label_m') : T.t('trend_label_a');
    const grouped = groupByDay(agg.count ? agg.rides : ridesN, locale);

    return el('div', { class: 'screen' }, [
        screenHeader(T.t('rides_title')),
        el('div', { class: 'screen-body' }, [
            el('div', null, [
                segmented({
                    value: RidesView.period,
                    onChange: v => { RidesView.period = v; render(); },
                    options: [
                        { value: 'week', label: T.t('period_week') },
                        { value: 'month', label: T.t('period_month') },
                        { value: 'all', label: T.t('period_all') },
                    ],
                }),
            ]),
            // Aggregate card
            card({}, [
                el('div', { class: 'kv-grid cols-3-even' }, [
                    el('div', { class: 'kv-block' }, [
                        el('div', { class: 'kv-label-eyebrow', text: T.t('total_distance') }),
                        el('div', { class: 'kv-value sz-34' }, [
                            agg.km.toFixed(1),
                            el('span', { class: 'unit', text: 'km' }),
                        ]),
                    ]),
                    el('div', { class: 'kv-block with-divider' }, [
                        el('div', { class: 'kv-label-eyebrow', text: T.t('total_rides') }),
                        el('div', { class: 'kv-value sz-24', text: String(agg.count) }),
                    ]),
                    el('div', { class: 'kv-block with-divider' }, [
                        el('div', { class: 'kv-label-eyebrow', text: T.t('total_time') }),
                        el('div', { class: 'kv-value sz-24 nowrap', text: fmt.durationMin(agg.minutes, locale) }),
                    ]),
                ]),
            ]),
            // Trend
            card({}, [
                el('div', { class: 'trend-head' }, [
                    el('div', { class: 'kv-label-eyebrow', text: trendLabel }),
                    el('div', { class: 'trend-meta' }, [
                        el('span', { style: { color: 'var(--text-dim)' } }, [
                            T.t('avg_per_ride') + ': ',
                            el('span', { class: 'v', text: agg.avgKm.toFixed(1) }),
                            ' km',
                        ]),
                    ]),
                ]),
                miniBars(buckets),
                el('div', { class: 'trend-axis' }, [
                    el('span', { text: RidesView.period === 'week' ? '−7d' : RidesView.period === 'month' ? '−30d' : '−12mo' }),
                    el('span', { text: T.t('now') }),
                ]),
            ]),
            // Explainer
            el('div', { class: 'explainer' }, [
                icon('i-signal', { size: 16, bold: true }),
                el('div', { class: 'explainer-body', text: T.t('rides_explanation') }),
            ]),
            // List or empty
            agg.count > 0
                ? el('div', null, grouped.map(g => el('div', null, [
                    sectionLabel(g.label),
                    card({ flush: true }, g.rides.map((r, i) =>
                        rideRow(r, i === g.rides.length - 1, locale)
                    )),
                  ])))
                : card({}, [
                    el('div', { class: 'empty-card' }, [
                        el('div', { class: 'empty-icon' }, [icon('i-route', { size: 22 })]),
                        el('div', { class: 'empty-title', text: T.t('rides_empty') }),
                        el('div', { class: 'empty-body', text: T.t('rides_empty_hint') }),
                    ]),
                ]),
        ]),
    ]);
}
function rideRow(r, last, locale) {
    const used = (r.batteryFrom != null && r.batteryTo != null)
        ? Math.max(0, r.batteryFrom - r.batteryTo) : null;
    return listRow({ last: last, onClick: () => openRide(r.id) }, [
        el('div', { class: 'ride-icon' }, [icon('i-route', { size: 18 })]),
        el('div', { class: 'ride-main' }, [
            el('div', { class: 'ride-km' }, [
                r.km.toFixed(1),
                el('span', { class: 'unit', text: 'km' }),
            ]),
            el('div', { class: 'ride-sub' }, [
                el('span', { text: fmt.timeOnly(r.start) }),
                el('span', { text: '·' }),
                el('span', { text: fmt.durationMin(r.durationMin, locale) }),
                r.avgKmh != null ? el('span', { text: '·' }) : null,
                r.avgKmh != null ? el('span', { text: r.avgKmh.toFixed(1) + ' km/h' }) : null,
            ]),
        ]),
        el('div', { class: 'ride-right' }, [
            used != null ? chip('-' + used + '%', { tone: 'warning', size: 'sm' }) : null,
            icon('i-chev-r', { size: 13 }),
        ]),
    ]);
}

// ─── Ride detail ─────────────────────────────────────────
function renderRideDetail() {
    const T = window.bridgeI18n;
    const locale = T.language;
    const ridesN = State.rides.map(normalizeRide);
    const ride = ridesN.find(r => r.id === Route.rideId);
    const back = el('button', {
        class: 'icon-btn', type: 'button',
        'aria-label': T.t('back'),
        onclick: () => closeRide(),
    }, [icon('i-chev-l', { size: 20, bold: true })]);

    if (!ride) {
        return el('div', { class: 'screen' }, [
            el('div', { class: 'detail-header' }, [
                el('div', { class: 'detail-header-row' }, [back, el('div', { class: 'detail-title', text: T.t('ride_detail') })]),
            ]),
            el('div', { class: 'screen-body', style: { padding: '20px' } }, [
                el('div', { style: { color: 'var(--text-dim)' }, text: 'Ride not found.' }),
            ]),
        ]);
    }

    const used = (ride.batteryFrom != null && ride.batteryTo != null)
        ? Math.max(0, ride.batteryFrom - ride.batteryTo) : null;

    return el('div', { class: 'screen' }, [
        el('div', { class: 'detail-header' }, [
            el('div', { class: 'detail-header-row' }, [back, el('div', { class: 'detail-title', text: T.t('ride_detail') })]),
        ]),
        el('div', { class: 'screen-body' }, [
            // Hero
            el('div', { class: 'card hero-card detail-hero-card' }, [
                el('div', { class: 'detail-hero-glow' }),
                el('div', { class: 'detail-hero-row' }, [
                    el('div', { class: 'detail-km' }, [
                        ride.km.toFixed(1),
                        el('span', { class: 'unit', text: 'km' }),
                    ]),
                    el('div', { class: 'detail-when', text: fmt.dateTime(ride.start, locale) }),
                ]),
            ]),
            // Duration
            card({}, [
                cardHeader(T.t('duration'), 'i-clock'),
                el('div', { class: 'duration-big', text: fmt.durationLong(ride.durationMin, locale) }),
                el('div', { class: 'kv-grid cols-2', style: { paddingTop: '14px', borderTop: '1px solid var(--border)' } }, [
                    timestampBlock(T.t('started'), ride.start, locale),
                    timestampBlock(T.t('ended'), ride.end, locale),
                ]),
            ]),
            // Speed
            card({}, [
                cardHeader(T.t('speed'), 'i-gauge'),
                el('div', { class: 'kv-grid cols-2' }, [
                    el('div', null, [
                        el('div', { class: 'kv-label-tiny', text: T.t('avg_speed') }),
                        el('div', { class: 'kv-value sz-28' }, [
                            ride.avgKmh != null ? ride.avgKmh.toFixed(1) : '—',
                            el('span', { class: 'unit', text: 'km/h' }),
                        ]),
                    ]),
                    el('div', null, [
                        el('div', { class: 'kv-label-tiny', text: T.t('max_speed') }),
                        el('div', { class: 'kv-value sz-28' }, [
                            ride.maxKmh != null ? ride.maxKmh.toFixed(1) : '—',
                            el('span', { class: 'unit', text: 'km/h' }),
                        ]),
                    ]),
                ]),
                el('div', { class: 'warn-note' }, [icon('i-alert', { size: 13 }), el('span', { text: T.t('ride_quality_note') })]),
            ]),
            // Battery delta
            used != null ? card({}, [
                cardHeader(T.t('battery'), 'i-battery'),
                el('div', { class: 'delta-row' }, [
                    el('div', { class: 'delta-num' }, [
                        '−' + used,
                        el('span', { class: 'unit', text: '%' }),
                    ]),
                    el('div', { style: { flex: 1 } }, [batteryDeltaBar(ride.batteryFrom, ride.batteryTo)]),
                ]),
                el('div', { class: 'kv-grid cols-2', style: { paddingTop: '14px', borderTop: '1px solid var(--border)' } }, [
                    miniStat(T.t('battery_at_start'), ride.batteryFrom, '%'),
                    miniStat(T.t('battery_at_end'), ride.batteryTo, '%'),
                ]),
            ]) : null,
        ]),
    ]);
}
function timestampBlock(label, ts, locale) {
    return el('div', { class: 'timestamp-block' }, [
        el('div', { class: 'kv-label-tiny', text: label }),
        el('div', { class: 'ts-time', text: fmt.timeOnly(ts) }),
        el('div', { class: 'ts-date', text: fmt.dateLong(ts, locale) }),
    ]);
}
function batteryDeltaBar(from, to) {
    return el('div', { class: 'delta-bar' }, [
        el('div', { class: 'end-fill', style: { width: to + '%' } }),
        el('div', { class: 'used', style: { left: to + '%', width: (from - to) + '%' } }),
    ]);
}

// ─── Settings ────────────────────────────────────────────
const SettingsView = {
    edits: null,         // local copy of config being edited
    savedFlash: false,
};

function ensureEdits() {
    if (!SettingsView.edits && State.config) {
        SettingsView.edits = JSON.parse(JSON.stringify(State.config));
    }
    return SettingsView.edits || {};
}

function renderSettings() {
    const T = window.bridgeI18n;
    const c = ensureEdits();
    const mqttConnected = State.bike && State.bike.mqtt_connected;
    const mqttStatus = mqttConnected ? 'connected' : 'disconnected';
    const serial = (State.bike && State.bike.serial) || T.t('not_available');

    function setEdit(k, v) { c[k] = v; }

    return el('div', { class: 'screen' }, [
        screenHeader(T.t('settings')),
        el('div', { class: 'screen-body gap-lg' }, [
            // Display
            el('div', null, [
                sectionLabel(T.t('sec_display'), 'i-dashboard'),
                card({}, [
                    field(T.t('language'), T.t('language_hint'),
                        selectInput({
                            value: T.language,
                            onChange: v => T.setLanguage(v),
                            options: [
                                { value: 'en', label: 'English' },
                                { value: 'nl', label: 'Nederlands' },
                            ],
                        })),
                    field(T.t('theme'), T.t('theme_hint'),
                        segmented({
                            value: T.theme,
                            onChange: v => T.setThemePreference(v),
                            options: [
                                { value: 'system', label: T.t('theme_system'), icon: 'i-monitor' },
                                { value: 'light', label: T.t('theme_light'), icon: 'i-sun' },
                                { value: 'dark', label: T.t('theme_dark'), icon: 'i-moon' },
                            ],
                        })),
                ]),
            ]),
            // Bike
            el('div', null, [
                sectionLabel(T.t('sec_bike'), 'i-bike'),
                card({}, [
                    field(T.t('bt_name'), T.t('bt_name_hint'),
                        textInput({ value: c.device_name || 'CGO600', onChange: v => setEdit('device_name', v) })),
                    field(T.t('poll_interval'), T.t('poll_interval_hint'),
                        numberInput({
                            value: c.poll_interval != null ? c.poll_interval : 1,
                            min: 0.5, max: 60, step: 0.5,
                            suffix: T.t('seconds'),
                            onChange: v => setEdit('poll_interval', v),
                        })),
                    field(T.t('bike_id'), T.t('bike_id_hint'),
                        textInput({ value: serial, mono: true, onChange: () => {} })),
                ]),
            ]),
            // Home Assistant
            el('div', null, [
                sectionLabel(T.t('sec_ha'), 'i-home'),
                card({}, [
                    el('div', { class: 'row-between', style: { marginBottom: '14px' } }, [
                        el('div', { class: 'field-hint', style: { flex: 1 }, text: T.t('mqtt_intro') }),
                        mqttStatusChip(mqttStatus),
                    ]),
                    field(T.t('mqtt_broker'), null,
                        textInput({ value: c.mqtt_broker || '', mono: true, onChange: v => setEdit('mqtt_broker', v) })),
                    el('div', { class: 'col-2' }, [
                        field(T.t('mqtt_port'), null,
                            numberInput({ value: c.mqtt_port || 1883, min: 1, max: 65535, step: 1, onChange: v => setEdit('mqtt_port', v) })),
                        field(T.t('mqtt_topic'), null,
                            textInput({ value: c.mqtt_prefix || 'cgo600', mono: true, onChange: v => setEdit('mqtt_prefix', v) })),
                    ]),
                    field(T.t('mqtt_username'), null,
                        textInput({ value: c.mqtt_user || '', mono: true, onChange: v => setEdit('mqtt_user', v) })),
                    field(T.t('mqtt_password'), null,
                        textInput({
                            value: '',
                            type: 'password',
                            placeholder: c.mqtt_pass_set ? '(' + T.t('mqtt_password_saved') + ')' : '••••••••',
                            onChange: v => setEdit('mqtt_pass', v),
                        })),
                ]),
            ]),
            // Ride history
            el('div', null, [
                sectionLabel(T.t('sec_history'), 'i-route'),
                card({}, [
                    field(T.t('max_rides'), T.t('max_rides_hint'),
                        numberInput({
                            value: c.max_rides != null ? c.max_rides : 100,
                            min: 10, max: 100, step: 10,
                            suffix: T.t('rides_unit'),
                            onChange: v => setEdit('max_rides', v),
                        })),
                    button(T.t('clear_rides'), {
                        danger: true,
                        icon: icon('i-trash', { size: 16 }),
                        onClick: clearRides,
                    }),
                ]),
            ]),
            // System
            el('div', null, [
                sectionLabel(T.t('sec_system'), 'i-refresh'),
                card({}, [
                    el('div', { class: 'field-hint', style: { marginBottom: '14px' }, text: T.t('restart_hint') }),
                    el('div', { style: { display: 'flex', gap: '8px', flexWrap: 'wrap' } }, [
                        button(SettingsView.savedFlash ? T.t('saved') : T.t('save'), {
                            icon: icon('i-save', { size: 16 }),
                            onClick: saveConfig,
                        }),
                        button(T.t('restart_bridge'), {
                            variant: 'secondary',
                            icon: icon('i-refresh', { size: 16 }),
                            onClick: restartBridge,
                        }),
                    ]),
                ]),
            ]),
        ]),
    ]);
}
function mqttStatusChip(status) {
    const T = window.bridgeI18n;
    const map = {
        connected:    { tone: 'success', label: T.t('mqtt_status_connected') },
        disconnected: { tone: 'neutral', label: T.t('mqtt_status_disconnected') },
    };
    const cfg = map[status] || map.disconnected;
    return el('span', { class: 'chip is-sm tone-' + cfg.tone }, [statusDot(cfg.tone), el('span', { text: cfg.label })]);
}

function saveConfig() {
    const T = window.bridgeI18n;
    const e = SettingsView.edits;
    if (!e) return;
    const payload = {
        mqtt_broker:  e.mqtt_broker || '',
        mqtt_port:    parseInt(e.mqtt_port, 10) || 1883,
        mqtt_user:    e.mqtt_user || '',
        mqtt_prefix:  e.mqtt_prefix || 'cgo600',
        device_name:  (e.device_name || 'CGO600').trim(),
        poll_interval: parseFloat(e.poll_interval) || 1.0,
        max_rides:    parseInt(e.max_rides, 10) || 100,
    };
    if (e.mqtt_pass !== undefined && e.mqtt_pass !== null && e.mqtt_pass !== '') {
        payload.mqtt_pass = e.mqtt_pass;
    }
    apiSendJson('/api/config', 'POST', payload)
        .then(r => r.json().catch(() => ({})))
        .then(b => {
            if (b && b.ok) {
                SettingsView.savedFlash = true;
                render();
                setTimeout(() => { SettingsView.savedFlash = false; render(); }, SAVED_FLASH_MS);
                refreshConfig();
            } else {
                toast(T.t('save_failed'), 'error');
            }
        })
        .catch(() => toast(T.t('save_failed'), 'error'));
}
function restartBridge() {
    const T = window.bridgeI18n;
    if (!confirm(T.t('restart_confirm'))) return;
    apiSendJson('/api/restart', 'POST', {})
        .then(() => toast(T.t('restarting'), 'warning'))
        .catch(() => toast(T.t('restarting'), 'warning'));
}
function clearRides() {
    const T = window.bridgeI18n;
    if (!confirm(T.t('clear_rides_confirm'))) return;
    fetch('/api/rides', { method: 'DELETE' })
        .then(r => {
            if (!r.ok) throw new Error();
            State.rides = [];
            toast(T.t('rides_deleted'), 'success');
            render();
        })
        .catch(() => toast(T.t('rides_delete_failed'), 'error'));
}

// ─── Tab bar ─────────────────────────────────────────────
function renderTabBar() {
    const T = window.bridgeI18n;
    const items = [
        { id: 'dashboard', label: T.t('tab_dashboard'), icon: 'i-dashboard' },
        { id: 'rides',     label: T.t('tab_rides'),     icon: 'i-route' },
        { id: 'settings',  label: T.t('tab_settings'),  icon: 'i-settings' },
    ];
    return el('div', { class: 'tab-bar-wrap' }, [
        el('div', { class: 'tab-bar' }, items.map(item => {
            const active = item.id === Route.tab;
            const b = el('button', {
                type: 'button',
                class: 'tab-btn' + (active ? ' is-active' : ''),
                onclick: () => setTab(item.id),
            }, [icon(item.icon, { size: 18, bold: true }), active ? el('span', { text: item.label }) : null]);
            return b;
        })),
    ]);
}

// ─── Render ──────────────────────────────────────────────
const root = document.getElementById('root');

function render() {
    clear(root);
    const showDetail = Route.rideId != null;

    const tabPane = el('div', { class: 'scroller-pane' + (showDetail ? ' is-back' : '') }, [
        Route.tab === 'dashboard' ? renderDashboard() :
        Route.tab === 'rides'     ? renderRides()     :
                                    renderSettings(),
    ]);
    const detailPane = el('div', { class: 'scroller-pane detail' + (showDetail ? ' is-open' : '') });
    if (showDetail) detailPane.appendChild(renderRideDetail());

    const stack = el('div', { class: 'scroller-stack' }, [tabPane, detailPane]);
    const app = el('div', { class: 'app' }, [stack]);
    if (!showDetail) app.appendChild(renderTabBar());

    root.appendChild(app);
}

// ─── Data flow ───────────────────────────────────────────
function refreshConfig() {
    return api('/api/config').then(c => {
        State.config = c;
        SettingsView.edits = null;
        render();
    }).catch(() => {});
}
function refreshRides() {
    return api('/api/rides').then(d => {
        State.rides = Array.isArray(d.rides) ? d.rides.slice() : [];
        State.rides.sort((a, b) => new Date(b.window_end).getTime() - new Date(a.window_end).getTime());
        render();
    }).catch(() => {});
}
function fetchStateOnce() {
    return api('/api/state').then(s => { State.bike = s; render(); }).catch(() => {});
}

// WebSocket for live state.
let ws = null, wsRetry = WS_RETRY_MIN_MS;
function connectWS() {
    if (ws && (ws.readyState === WebSocket.OPEN || ws.readyState === WebSocket.CONNECTING)) return;
    const proto = location.protocol === 'https:' ? 'wss:' : 'ws:';
    try { ws = new WebSocket(proto + '//' + location.host + '/ws'); } catch (e) { return; }
    ws.onopen = () => { wsRetry = WS_RETRY_MIN_MS; };
    ws.onmessage = ev => {
        try { State.bike = Object.assign({}, State.bike, JSON.parse(ev.data)); render(); }
        catch (e) {}
    };
    ws.onerror = () => { if (ws) ws.close(); };
    ws.onclose = () => { ws = null; setTimeout(connectWS, wsRetry); wsRetry = Math.min(wsRetry * 2, WS_RETRY_MAX_MS); };
}

// Re-render on language/theme changes.
window.addEventListener('languagechange', render);
window.addEventListener('themechange',   render);

// Boot.
refreshConfig().then(() => {
    refreshRides();
    fetchStateOnce();
    connectWS();
    render();
});
setInterval(() => { if (!ws || ws.readyState !== WebSocket.OPEN) fetchStateOnce(); }, STATE_POLL_MS);
setInterval(refreshRides, RIDES_REFRESH_MS);

if ('serviceWorker' in navigator) {
    navigator.serviceWorker.register('/sw.js').catch(() => {});
}

// Initial render with shell so the page isn't blank while loading.
render();

})();
