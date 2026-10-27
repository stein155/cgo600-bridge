// ─── Settings ────────────────────────────────────────────
// Bridge settings live in the ESPHome configuration; this screen shows them
// read-only and keeps the display preferences, ride history and restart.
function renderSettings() {
    const T = window.bridgeI18n;
    const c = State.config || {};
    const apiConnected = !!(State.bike && State.bike.api_connected);
    const serial = (State.bike && State.bike.serial) || T.t('not_available');

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
                    field(T.t('bike_id'), T.t('bike_id_hint'),
                        textInput({ value: serial, mono: true, readonly: true })),
                ]),
            ]),
            // Home Assistant
            el('div', null, [
                sectionLabel(T.t('sec_ha'), 'i-home'),
                card({}, [
                    el('div', { class: 'row-between' }, [
                        el('div', { class: 'field-hint', style: { flex: 1 }, text: T.t('api_intro') }),
                        apiStatusChip(apiConnected),
                    ]),
                ]),
            ]),
            // Ride history
            el('div', null, [
                sectionLabel(T.t('sec_history'), 'i-route'),
                card({}, [
                    field(T.t('max_rides'), T.t('yaml_hint'),
                        textInput({ value: c.max_rides != null ? c.max_rides : 20, readonly: true, suffix: T.t('rides_unit') })),
                    el('div', { style: { display: 'flex', gap: '8px', flexWrap: 'wrap' } }, [
                        button(T.t('import_rides'), {
                            variant: 'secondary',
                            icon: icon('i-save', { size: 16 }),
                            onClick: importRides,
                        }),
                        button(T.t('clear_rides'), {
                            danger: true,
                            icon: icon('i-trash', { size: 16 }),
                            onClick: clearRides,
                        }),
                    ]),
                    el('div', { class: 'field-hint', style: { marginTop: '10px' }, text: T.t('import_hint') }),
                ]),
            ]),
            // System
            el('div', null, [
                sectionLabel(T.t('sec_system'), 'i-refresh'),
                card({}, [
                    field(T.t('address'), T.t('address_hint'),
                        textInput({ value: bridgeAddress(c), mono: true, readonly: true })),
                    el('div', { class: 'field-hint', style: { marginBottom: '14px' }, text: T.t('restart_hint') }),
                    button(T.t('restart_bridge'), {
                        variant: 'secondary',
                        icon: icon('i-refresh', { size: 16 }),
                        onClick: restartBridge,
                    }),
                ]),
            ]),
        ]),
    ]);
}
// The IP address works even where mDNS (.local) names do not resolve.
function bridgeAddress(c) {
    const parts = [];
    if (c.ip) parts.push('http://' + c.ip);
    if (c.hostname) parts.push(c.hostname);
    return parts.join('  ·  ') || location.host;
}
function apiStatusChip(connected) {
    const T = window.bridgeI18n;
    const tone = connected ? 'success' : 'neutral';
    const label = connected ? T.t('api_status_connected') : T.t('api_status_disconnected');
    return el('span', { class: 'chip is-sm tone-' + tone }, [statusDot(tone), el('span', { text: label })]);
}

function restartBridge() {
    const T = window.bridgeI18n;
    if (!confirm(T.t('restart_confirm'))) return;
    fetch('/api/restart', { method: 'POST' })
        .then(() => toast(T.t('restarting'), 'warning'))
        .catch(() => toast(T.t('restarting'), 'warning'));
}
function clearRides() {
    const T = window.bridgeI18n;
    if (!confirm(T.t('clear_rides_confirm'))) return;
    fetch('/api/rides/clear', { method: 'POST' })
        .then(r => {
            if (!r.ok) throw new Error();
            State.rides = [];
            toast(T.t('rides_deleted'), 'success');
            render();
        })
        .catch(() => toast(T.t('rides_delete_failed'), 'error'));
}

// Imports a ride export ({"rides": [...]}, as served by /api/rides) one ride
// per request; the bridge ignores rides it already has.
function importRides() {
    const T = window.bridgeI18n;
    const input = el('input', { type: 'file', accept: 'application/json,.json' });
    input.addEventListener('change', () => {
        const file = input.files && input.files[0];
        if (!file) return;
        file.text()
            .then(text => {
                const rides = (JSON.parse(text).rides || []).filter(r => r.window_start && r.window_end);
                return rides.reduce((chain, r) => chain.then(() => postRide(r)), Promise.resolve()).then(() => rides.length);
            })
            .then(n => {
                toast(T.t('import_done').replace('{n}', n), 'success');
                setTimeout(refreshRides, 1000);
            })
            .catch(() => toast(T.t('import_failed'), 'error'));
    });
    input.click();
}
function postRide(r) {
    const epoch = iso => Math.round(new Date(iso).getTime() / 1000);
    const q = new URLSearchParams({
        start: epoch(r.window_start),
        end: epoch(r.window_end),
        distance: r.distance_km || 0,
        duration: r.ride_duration_sec || 0,
    });
    if (r.battery_start != null) q.set('battery_start', r.battery_start);
    if (r.battery_end != null) q.set('battery_end', r.battery_end);
    if (r.avg_speed_kmh != null) q.set('avg', r.avg_speed_kmh);
    if (r.max_speed_kmh != null) q.set('max', r.max_speed_kmh);
    return fetch('/api/rides/import?' + q.toString(), { method: 'POST' }).then(res => {
        if (!res.ok) throw new Error('HTTP ' + res.status);
    });
}
