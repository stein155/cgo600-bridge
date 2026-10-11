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
