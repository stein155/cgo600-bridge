// ─── Formatters ──────────────────────────────────────────
const fmt = {
    km(n) { return (Number(n) || 0).toFixed(1); },
    kmInt(n) { return Math.round(Number(n) || 0).toString(); },
    hUnit(locale) { return locale === 'nl' ? 'u' : 'h'; },
    durationMin(min, locale) {
        min = Math.max(0, Math.round(min));
        const h = Math.floor(min / 60), m = min % 60;
        if (h === 0) return m + 'm';
        return h + fmt.hUnit(locale) + ' ' + m + 'm';
    },
    durationLong(min, locale) {
        min = Math.max(0, Math.round(min));
        const h = Math.floor(min / 60), m = min % 60;
        if (h === 0) return m + 'm';
        return h + fmt.hUnit(locale) + ' ' + m + 'm';
    },
    saddleTime(min) {
        min = Math.max(0, Math.round(min));
        return { h: Math.floor(min / 60), m: min % 60 };
    },
    relative(ts, locale) {
        if (!ts) return '';
        const diff = Date.now() - (typeof ts === 'number' ? ts : new Date(ts).getTime());
        const min = Math.round(diff / MINUTE_MS);
        const T = window.bridgeI18n;
        if (isNaN(min)) return '';
        if (min < 1) return T.t('just_now');
        if (min < 60) return T.t('minutes_ago', min);
        const h = Math.round(min / 60);
        if (h < 24) return T.t('hours_ago', h);
        const d = Math.round(h / 24);
        return T.t('days_ago', d);
    },
    timeOnly(ts) {
        const d = new Date(ts);
        if (isNaN(d.getTime())) return '';
        return d.toTimeString().slice(0, 5);
    },
    dateTime(ts, locale) {
        const d = new Date(ts);
        if (isNaN(d.getTime())) return '';
        const time = d.toTimeString().slice(0, 5);
        if (locale === 'nl') {
            const pad = n => n < 10 ? '0' + n : '' + n;
            return pad(d.getDate()) + '-' + pad(d.getMonth() + 1) + '-' + d.getFullYear() + ', ' + time;
        }
        return d.toLocaleString('en', { month: 'short' }) + ' ' + d.getDate() + ', ' + time;
    },
    dateLong(ts, locale) {
        const d = new Date(ts);
        if (isNaN(d.getTime())) return '';
        return d.toLocaleString(locale === 'nl' ? 'nl-NL' : 'en-US', { weekday: 'short', day: 'numeric', month: 'short' });
    },
};
