// CGO600 Bridge — translations.
// Keys mirror the design prototype (test-t/project/i18n.js).

const I18N_LANGS = ['en', 'nl'];

const I18N = {
    en: {
        tab_dashboard: 'Dashboard',
        tab_rides: 'Rides',
        tab_settings: 'Settings',

        status_connected: 'Connected',
        status_searching: 'Searching',
        status_offline: 'Offline',
        status_connected_hint: 'In range and responding',
        status_searching_hint: 'Bring the bike closer and switch it on',
        status_offline_hint: 'Out of range since last check-in',

        just_now: 'Just now',
        minutes_ago: function (n) { return n + ' min ago'; },
        hours_ago:   function (n) { return n + 'h ago'; },
        days_ago:    function (n) { return n + 'd ago'; },

        battery: 'Battery',
        range: 'Range',

        light: 'Light',
        light_on: 'On',
        light_off: 'Off',
        light_only_connected: 'Connect to the bike to control the light',

        last_ride: 'Last ride',
        no_last_ride: 'No rides recorded yet',
        no_last_ride_hint: 'A ride is logged when the bike reconnects after being away.',
        view_all_rides: 'View all rides',

        lifetime: 'Lifetime',
        total_distance: 'Distance',
        total_time: 'Time',
        total_rides: 'Rides',

        rides_title: 'Rides',
        rides_explanation: 'The bridge only sees the bike when it reconnects, so rides are inferred from the time and distance between two check-ins.',
        period_week: 'Week',
        period_month: 'Month',
        period_all: 'All',
        avg_per_ride: 'Avg per ride',
        trend_label: 'Distance per day',
        trend_label_m: 'Distance per day',
        trend_label_a: 'Distance per month',
        rides_empty: 'No rides yet',
        rides_empty_hint: 'Take a ride. When the bike reconnects to the bridge, you’ll see it here.',

        ride_detail: 'Ride',
        started: 'Started',
        ended: 'Ended',
        duration: 'Duration',
        speed: 'Speed',
        avg_speed: 'Avg speed',
        max_speed: 'Top speed',
        battery_used: 'Battery used',
        battery_at_start: 'At start',
        battery_at_end: 'At end',
        seen_parked: 'Last seen parked',
        seen_again: 'Seen again',
        time_unknown: 'Time unknown',
        ride_time_estimated_note: 'The bridge didn’t see the bike leave, so the start is estimated from the riding time.',
        ride_time_unknown_note: 'The bridge saw neither the departure nor the arrival. The ride took place somewhere between these two check-ins; speeds are unknown.',
        ride_quality_note: 'Average is computed over the full window — brief stops aren’t detected. If the bike is powered off and back on mid-ride, the counters reset and this ride may be incomplete.',

        settings: 'Settings',
        sec_display: 'Display',
        sec_bike: 'Bike',
        sec_ha: 'Home Assistant',
        sec_history: 'Ride history',
        sec_system: 'System',

        language: 'Language',
        language_hint: 'Used across the bridge UI.',
        theme: 'Theme',
        theme_hint: 'Light, dark, or match your device.',
        theme_system: 'System',
        theme_light: 'Light',
        theme_dark: 'Dark',

        bt_name: 'Bluetooth name',
        bt_name_hint: 'What the bridge looks for. Default: CGO600.',
        poll_interval: 'Poll interval',
        poll_interval_hint: 'How often the bridge wakes the bike. Lower drains the bike faster.',
        seconds: 'seconds',
        bike_id: 'Bike ID',
        bike_id_hint: 'Auto-detected. Shown for reference.',
        not_available: 'Not available yet',

        yaml_hint: 'Set in the ESPHome configuration.',
        api_intro: 'The bridge reports to Home Assistant through the ESPHome integration.',
        api_status_connected: 'Connected',
        api_status_disconnected: 'Not connected',

        max_rides: 'Keep last',
        max_rides_hint: 'Older rides are dropped when this limit is hit.',
        rides_unit: 'rides',
        clear_rides: 'Clear all rides',
        clear_rides_confirm: 'Delete every stored ride? This cannot be undone.',
        rides_deleted: 'Rides deleted',
        rides_delete_failed: 'Failed to delete rides',
        import_rides: 'Import rides',
        import_hint: 'Import a ride export (rides.json) from an earlier bridge. Rides already stored are skipped.',
        import_done: 'Imported {n} rides',
        import_failed: 'Import failed',

        address: 'Address',
        address_hint: 'Bookmark this address to open the dashboard.',
        restart_bridge: 'Restart bridge',
        restart_hint: 'Restarts the bridge. Wi‑Fi and other settings are managed in ESPHome.',
        restart_confirm: 'Restart the bridge now?',
        restarting: 'Restarting…',
        save_failed: 'Failed to save',

        back: 'Back',
        now: 'now',
    },

    nl: {
        tab_dashboard: 'Dashboard',
        tab_rides: 'Ritten',
        tab_settings: 'Instellingen',

        status_connected: 'Verbonden',
        status_searching: 'Zoeken',
        status_offline: 'Offline',
        status_connected_hint: 'In bereik en reageert',
        status_searching_hint: 'Houd de fiets dichtbij en ingeschakeld',
        status_offline_hint: 'Sinds laatste check-in buiten bereik',

        just_now: 'Zojuist',
        minutes_ago: function (n) { return n + ' min geleden'; },
        hours_ago:   function (n) { return n + ' u geleden'; },
        days_ago:    function (n) { return n + ' d geleden'; },

        battery: 'Batterij',
        range: 'Bereik',

        light: 'Licht',
        light_on: 'Aan',
        light_off: 'Uit',
        light_only_connected: 'Maak verbinding met de fiets om het licht te bedienen',

        last_ride: 'Laatste rit',
        no_last_ride: 'Nog geen ritten',
        no_last_ride_hint: 'Een rit wordt geregistreerd zodra de fiets opnieuw verbindt.',
        view_all_rides: 'Bekijk alle ritten',

        lifetime: 'Totaal',
        total_distance: 'Afstand',
        total_time: 'Tijd',
        total_rides: 'Ritten',

        rides_title: 'Ritten',
        rides_explanation: 'De bridge ziet de fiets alleen bij herverbinding, dus ritten worden afgeleid uit het tijds‑ en afstandsverschil tussen twee check-ins.',
        period_week: 'Week',
        period_month: 'Maand',
        period_all: 'Altijd',
        avg_per_ride: 'Gem. per rit',
        trend_label: 'Afstand per dag',
        trend_label_m: 'Afstand per dag',
        trend_label_a: 'Afstand per maand',
        rides_empty: 'Nog geen ritten',
        rides_empty_hint: 'Maak een rit. Bij herverbinding zie je hem hier.',

        ride_detail: 'Rit',
        started: 'Begonnen',
        ended: 'Beëindigd',
        duration: 'Duur',
        speed: 'Snelheid',
        avg_speed: 'Gem. snelheid',
        max_speed: 'Max snelheid',
        battery_used: 'Batterij gebruikt',
        battery_at_start: 'Bij start',
        battery_at_end: 'Bij eind',
        seen_parked: 'Laatst geparkeerd gezien',
        seen_again: 'Weer gezien',
        time_unknown: 'Tijd onbekend',
        ride_time_estimated_note: 'De bridge zag de fiets niet vertrekken, dus de starttijd is geschat op basis van de rijtijd.',
        ride_time_unknown_note: 'De bridge zag vertrek noch aankomst. De rit vond ergens tussen deze twee check-ins plaats; snelheden zijn onbekend.',
        ride_quality_note: 'Snelheid is gemiddeld over het hele venster — korte stops zien we niet. Wordt de fiets tussentijds uit‑ en aangezet, dan resetten de tellers en kan deze rit incompleet zijn.',

        settings: 'Instellingen',
        sec_display: 'Weergave',
        sec_bike: 'Fiets',
        sec_ha: 'Home Assistant',
        sec_history: 'Ritgeschiedenis',
        sec_system: 'Systeem',

        language: 'Taal',
        language_hint: 'Gebruikt in de hele bridge.',
        theme: 'Thema',
        theme_hint: 'Licht, donker, of volg je apparaat.',
        theme_system: 'Systeem',
        theme_light: 'Licht',
        theme_dark: 'Donker',

        bt_name: 'Bluetooth‑naam',
        bt_name_hint: 'Waar de bridge naar zoekt. Standaard: CGO600.',
        poll_interval: 'Poll‑interval',
        poll_interval_hint: 'Hoe vaak de bridge de fiets aanspreekt. Lager = sneller leeg.',
        seconds: 'seconden',
        bike_id: 'Fiets‑ID',
        bike_id_hint: 'Automatisch gedetecteerd.',
        not_available: 'Nog niet beschikbaar',

        yaml_hint: 'In te stellen in de ESPHome-configuratie.',
        api_intro: 'De bridge rapporteert aan Home Assistant via de ESPHome-integratie.',
        api_status_connected: 'Verbonden',
        api_status_disconnected: 'Niet verbonden',

        max_rides: 'Bewaar laatste',
        max_rides_hint: 'Oudere ritten worden verwijderd bij overschrijding.',
        rides_unit: 'ritten',
        clear_rides: 'Wis alle ritten',
        clear_rides_confirm: 'Alle opgeslagen ritten verwijderen?',
        rides_deleted: 'Ritten gewist',
        rides_delete_failed: 'Wissen mislukt',
        import_rides: 'Ritten importeren',
        import_hint: 'Importeer een rittenexport (rides.json) van een eerdere bridge. Ritten die al bestaan worden overgeslagen.',
        import_done: '{n} ritten geïmporteerd',
        import_failed: 'Importeren mislukt',

        address: 'Adres',
        address_hint: 'Bewaar dit adres om het dashboard te openen.',
        restart_bridge: 'Herstart bridge',
        restart_hint: 'Herstart de bridge. Wi‑Fi en andere instellingen beheer je in ESPHome.',
        restart_confirm: 'Bridge nu herstarten?',
        restarting: 'Herstarten…',
        save_failed: 'Opslaan mislukt',

        back: 'Terug',
        now: 'nu',
    },
};

(function () {
    function detectLanguage() {
        const saved = localStorage.getItem('cgo_language');
        if (saved && I18N_LANGS.indexOf(saved) >= 0) return saved;
        const nav = (navigator.language || 'en').toLowerCase();
        for (const l of I18N_LANGS) if (nav.indexOf(l) === 0) return l;
        return 'en';
    }
    function detectTheme() {
        const saved = localStorage.getItem('cgo_theme');
        if (saved && ['system', 'light', 'dark'].indexOf(saved) >= 0) return saved;
        return 'system';
    }
    function applyTheme(pref) {
        const root = document.documentElement;
        let mode = pref;
        if (pref === 'system') {
            mode = (window.matchMedia && window.matchMedia('(prefers-color-scheme: dark)').matches) ? 'dark' : 'light';
        }
        root.setAttribute('data-theme', mode);
    }

    const state = {
        language: detectLanguage(),
        theme: detectTheme(),
    };
    applyTheme(state.theme);
    document.documentElement.setAttribute('lang', state.language);

    if (window.matchMedia) {
        const mq = window.matchMedia('(prefers-color-scheme: dark)');
        const fn = () => { if (state.theme === 'system') applyTheme('system'); };
        if (mq.addEventListener) mq.addEventListener('change', fn);
        else if (mq.addListener) mq.addListener(fn);
    }

    window.bridgeI18n = {
        get language() { return state.language; },
        get theme() { return state.theme; },
        t(key, ...args) {
            const dict = I18N[state.language] || I18N.en;
            const v = dict[key];
            if (typeof v === 'function') return v(...args);
            if (v !== undefined) return v;
            const fb = I18N.en[key];
            return typeof fb === 'function' ? fb(...args) : (fb !== undefined ? fb : key);
        },
        setLanguage(lang) {
            if (I18N_LANGS.indexOf(lang) < 0) return;
            state.language = lang;
            localStorage.setItem('cgo_language', lang);
            document.documentElement.setAttribute('lang', lang);
            window.dispatchEvent(new CustomEvent('languagechange'));
        },
        setThemePreference(pref) {
            if (['system', 'light', 'dark'].indexOf(pref) < 0) return;
            state.theme = pref;
            localStorage.setItem('cgo_theme', pref);
            applyTheme(pref);
            window.dispatchEvent(new CustomEvent('themechange'));
        },
        themeMode() {
            return document.documentElement.getAttribute('data-theme') || 'dark';
        },
    };
})();
