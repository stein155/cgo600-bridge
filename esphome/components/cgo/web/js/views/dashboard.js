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
        screenHeader(T.t('tab_dashboard')),
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
                fetch('/api/light?on=' + (next ? 1 : 0), { method: 'POST' })
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
