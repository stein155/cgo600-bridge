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

// Re-render on language/theme changes.
window.addEventListener('languagechange', render);
window.addEventListener('themechange',   render);

// Boot.
refreshConfig().then(() => {
    refreshRides();
    fetchStateOnce();
    render();
});
// Live state by polling; skip while the page is hidden to spare the bridge.
setInterval(() => { if (!document.hidden) fetchStateOnce(); }, STATE_POLL_MS);
setInterval(refreshRides, RIDES_REFRESH_MS);

if ('serviceWorker' in navigator) {
    navigator.serviceWorker.register('/sw.js').catch(() => {});
}

// Initial render with shell so the page isn't blank while loading.
render();
