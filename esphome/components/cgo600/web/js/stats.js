// ─── Aggregation ─────────────────────────────────────────
// A window this much longer than the riding time includes parked time.
const LONG_WINDOW_SLACK_MS = 20 * MINUTE_MS;
// The firmware advances window_start on every parked check-in. When it stays
// this close to the previous ride's end, the bridge never saw the departure.
const MISSED_DEPARTURE_GAP_MS = 15 * MINUTE_MS;

// window_start is the last check-in before the ride and window_end the first
// one after it. They are the departure and arrival only when the bridge saw
// both; timing says how far to trust them:
//   'observed'  - departure seen (or the window matches the riding time)
//   'estimated' - departure missed; start is window_end − riding time
//   'unknown'   - the ride was logged when the bike was switched on (no
//                 speeds), so the arrival was missed too and the ride took
//                 place somewhere inside the window
// Called as rides.map(normalizeRide); rides are newest first.
function normalizeRide(r, idx, rides) {
    const windowEnd = new Date(r.window_end).getTime();
    let windowStart = new Date(r.window_start).getTime();
    if (isNaN(windowStart)) windowStart = windowEnd;
    const durationSec = Number(r.ride_duration_sec) || 0;
    const maxKmh = r.max_speed_kmh != null ? Number(r.max_speed_kmh) : null;
    const noSpeeds = maxKmh == null || maxKmh <= 0;

    const prev = rides && rides[idx + 1];
    const prevEnd = prev ? new Date(prev.window_end).getTime() : NaN;
    const departureMissed = isNaN(prevEnd) || windowStart - prevEnd < MISSED_DEPARTURE_GAP_MS;

    let timing = 'observed', start = windowStart;
    if (windowEnd - windowStart > durationSec * 1000 + LONG_WINDOW_SLACK_MS) {
        if (noSpeeds) {
            timing = 'unknown';
        } else if (departureMissed) {
            timing = 'estimated';
            start = windowEnd - durationSec * 1000;
        }
    }

    let batteryFrom = r.battery_start != null ? Number(r.battery_start) : null;
    let batteryTo = r.battery_end != null ? Number(r.battery_end) : null;
    // Charged somewhere inside the window: the battery used is unknown.
    if (batteryFrom != null && batteryTo != null && batteryTo > batteryFrom) {
        batteryFrom = batteryTo = null;
    }

    return {
        id: r.id || ('ride-' + idx),
        raw: r,
        start: start,
        end: windowEnd,
        windowStart: windowStart,
        windowEnd: windowEnd,
        timing: timing,
        km: Number(r.distance_km) || 0,
        durationMin: Math.round(durationSec / 60),
        avgKmh: !noSpeeds && r.avg_speed_kmh != null ? Number(r.avg_speed_kmh) : null,
        maxKmh: noSpeeds ? null : maxKmh,
        batteryFrom: batteryFrom,
        batteryTo: batteryTo,
    };
}
function lifetime(rides) {
    let km = 0, minutes = 0;
    for (const r of rides) { km += r.km; minutes += r.durationMin; }
    return { km: +km.toFixed(1), minutes: minutes, count: rides.length };
}
function aggregateRides(rides, period) {
    const now = Date.now(), DAY = DAY_MS;
    let cutoff = 0;
    if (period === 'week') cutoff = now - 7 * DAY;
    else if (period === 'month') cutoff = now - 30 * DAY;
    const within = rides.filter(r => r.end >= cutoff);
    const km = within.reduce((a, r) => a + r.km, 0);
    const minutes = within.reduce((a, r) => a + r.durationMin, 0);
    return {
        rides: within,
        count: within.length,
        km: +km.toFixed(1),
        minutes: minutes,
        avgKm: within.length ? +(km / within.length).toFixed(1) : 0,
    };
}
function trendBuckets(rides, period) {
    const now = Date.now(), DAY = DAY_MS;
    let bucketMs, n;
    if (period === 'week') { bucketMs = DAY; n = 7; }
    else if (period === 'month') { bucketMs = DAY; n = 30; }
    else { bucketMs = 30 * DAY; n = 12; }
    const buckets = new Array(n).fill(0);
    for (const r of rides) {
        const ageMs = now - r.end;
        const idx = n - 1 - Math.floor(ageMs / bucketMs);
        if (idx >= 0 && idx < n) buckets[idx] += r.km;
    }
    return buckets.map(v => +v.toFixed(1));
}
function groupByDay(rides, locale) {
    const groups = [];
    const byKey = {};
    for (const r of rides) {
        const d = new Date(r.end);
        const key = d.getFullYear() + '-' + d.getMonth() + '-' + d.getDate();
        if (!byKey[key]) {
            const today = new Date();
            const yest = new Date(today.getTime() - DAY_MS);
            let label;
            if (today.toDateString() === d.toDateString()) label = locale === 'nl' ? 'Vandaag' : 'Today';
            else if (yest.toDateString() === d.toDateString()) label = locale === 'nl' ? 'Gisteren' : 'Yesterday';
            else label = d.toLocaleString(locale === 'nl' ? 'nl-NL' : 'en-US', { weekday: 'long', month: 'short', day: 'numeric' });
            const g = { label: label, rides: [] };
            byKey[key] = g;
            groups.push(g);
        }
        byKey[key].rides.push(r);
    }
    return groups;
}
