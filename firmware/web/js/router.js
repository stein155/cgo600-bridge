// ─── Routing ─────────────────────────────────────────────
const Route = {
    tab: 'dashboard',
    rideId: null,
};

function setTab(t) { Route.tab = t; Route.rideId = null; render(); }
function openRide(id) { Route.rideId = id; render(); }
function closeRide() { Route.rideId = null; render(); }
