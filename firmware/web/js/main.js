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
