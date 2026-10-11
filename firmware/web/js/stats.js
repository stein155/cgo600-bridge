// ─── Aggregation ─────────────────────────────────────────
function rideStart(r) {
    // window_start is the departure moment recorded by the firmware (last time
    // the bike was seen before it dropped out of BLE range). Use it directly —
    // do NOT estimate from window_end − duration.
    const start = new Date(r.window_start).getTime();
    if (!isNaN(start)) return start;
    return new Date(r.window_end).getTime();
}
function rideEnd(r) { return new Date(r.window_end).getTime(); }
function normalizeRide(r, idx) {
    return {
        id: r.id || ('ride-' + idx),
        raw: r,
        start: rideStart(r),
        end: rideEnd(r),
        km: Number(r.distance_km) || 0,
        durationMin: Math.round((Number(r.ride_duration_sec) || 0) / 60),
        avgKmh: r.avg_speed_kmh != null ? Number(r.avg_speed_kmh) : null,
        maxKmh: r.max_speed_kmh != null ? Number(r.max_speed_kmh) : null,
        batteryFrom: r.battery_start != null ? Number(r.battery_start) : null,
        batteryTo: r.battery_end != null ? Number(r.battery_end) : null,
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
