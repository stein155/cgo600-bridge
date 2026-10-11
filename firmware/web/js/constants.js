// ─── Timing constants (ms) ───────────────────────────────
const MINUTE_MS = 60000;
const DAY_MS = 86400000;
const TOAST_MS = 2800;          // toast auto-dismiss
const SAVED_FLASH_MS = 1400;    // "Saved" confirmation flash
const WS_RETRY_MIN_MS = 1000;   // WebSocket reconnect backoff floor
const WS_RETRY_MAX_MS = 5000;   // WebSocket reconnect backoff ceiling
const STATE_POLL_MS = 5000;     // fallback /api/state poll when WS is down
const RIDES_REFRESH_MS = 30000; // periodic ride-list refresh
