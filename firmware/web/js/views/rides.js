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
