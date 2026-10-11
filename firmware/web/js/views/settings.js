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
