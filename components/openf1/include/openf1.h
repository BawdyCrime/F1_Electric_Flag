#pragma once

#include "esp_err.h"
#include "f1_types.h"

#include <ctime>
#include <vector>

namespace openf1 {

// Optional. Without credentials, only unauthenticated requests are made.
void set_credentials(const char *login, const char *password);

// Blocking calls; must not be invoked from the UI/touch context.
esp_err_t fetch_meetings(int year, std::vector<f1::Meeting> *meetings);
esp_err_t fetch_sessions(uint32_t meeting_key, std::vector<f1::Session> *sessions);

// Session constants (fetch once per session).
esp_err_t fetch_drivers(uint32_t session_key, std::vector<f1::Driver> *drivers);
esp_err_t fetch_stints(uint32_t session_key, std::vector<f1::Stint> *stints);

// Replay: timing state at `at_epoch` (UTC). Looks back `window_s` seconds for the latest sample per driver.
esp_err_t fetch_snapshot(uint32_t session_key, time_t at_epoch, int window_s, f1::TimingSnapshot *snapshot);

// Live: changes in (since_epoch, until_epoch]; since 0 = everything since the session start, for the first call.
// until_epoch is "now" minus the user's broadcast delay.
// Positions/intervals are partial (only drivers with new rows); merge them into the caller's state.
// Requires a subscription during the session; the same endpoints are free once the data is historical.
esp_err_t fetch_live_update(uint32_t session_key, time_t since_epoch, time_t until_epoch, f1::TimingSnapshot *update);

// Completed laps (with a duration) whose start lies in [from_epoch, to_epoch].
esp_err_t fetch_laps(uint32_t session_key, time_t from_epoch, time_t to_epoch, std::vector<f1::LapTime> *laps);

// Running periods from race control "SESSION STARTED"/"SESSION FINISHED" messages, in order.
esp_err_t fetch_segments(uint32_t session_key, std::vector<f1::Segment> *segments);

}  // namespace openf1
