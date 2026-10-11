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
