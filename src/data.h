// data.h — network data services (NTP time, weather, flights) feeding the UI.
#pragma once

// Start NTP time sync. Call once after Wi-Fi connects.
void data_begin_time();

// Periodic poller: refreshes only the focused tab's data source on its cadence,
// and syncs immediately when a tab gains focus. Safe to call every loop(); it
// self-throttles and no-ops when offline.
void data_tick();

// Request a one-off flights refresh on the next loop tick (e.g. after a zoom
// change) without waiting for the normal poll cadence.
void data_request_flights();

// Request a one-off tickers refresh (e.g. after the chart timeframe changed).
void data_request_tickers();
