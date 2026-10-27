// ─── Render ──────────────────────────────────────────────
const root = document.getElementById('root');
let renderedView = null;

function render() {
    // Rebuilding replaces the scroll containers, so carry their positions over
    // while the same screen stays open.
    const view = Route.tab + '/' + Route.rideId;
    const scrollTops = view === renderedView ? Array.from(root.querySelectorAll('.screen'), s => s.scrollTop) : [];
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
    root.querySelectorAll('.screen').forEach((s, i) => { if (scrollTops[i]) s.scrollTop = scrollTops[i]; });
    renderedView = view;
}

// A rebuild would take the focus out of a field the user is in.
function isEditing() {
    const a = document.activeElement;
    return !!a && root.contains(a) && ['INPUT', 'SELECT', 'TEXTAREA'].indexOf(a.tagName) >= 0;
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
        if (!isEditing()) render();
    }).catch(() => {});
}
function fetchStateOnce() {
    return api('/api/state').then(s => { State.bike = s; if (!isEditing()) render(); }).catch(() => {});
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
