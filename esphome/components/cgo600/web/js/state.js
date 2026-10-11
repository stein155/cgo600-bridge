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
