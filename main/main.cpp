#include "app_time.h"
#include "board_setup.h"
#include "bsp_board.h"
#include "credentials.h"
#include "event_timing.h"
#include "f1_types.h"
#include "flag_display.h"
#include "openf1.h"
#include "session_selector.h"
#include "touch_input.h"

#include "esp_log.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"
#include "freertos/task.h"

#include <algorithm>
#include <array>
#include <strings.h>
#include <cstdio>
#include <ctime>
#include <string>
#include <vector>

static const char *TAG = "main";

// Data-flow control: screen and OpenF1 client never talk to each other.
// The screen reports user actions as requests; main fetches data and hands it to the screen.
enum class request_type_t : uint8_t {
    load_meetings,
    load_sessions,
    show_timing,
    refresh_replay,
};

struct request_t {
    request_type_t type;
    uint32_t meeting_key;
    uint32_t session_key;
};

static QueueHandle_t s_requests = nullptr;
static int s_year = 0;
// Only accessed from data_task.
static std::vector<f1::Session> s_sessions;

static void post_request(request_type_t type, uint32_t meeting_key, uint32_t session_key = 0)
{
    const request_t request = {type, meeting_key, session_key};
    if (s_requests == nullptr || xQueueSend(s_requests, &request, 0) != pdPASS) {
        ESP_LOGW(TAG, "Request queue full, dropping request");
    }
}

static void on_meeting_selected(uint32_t meeting_key, void *)
{
    post_request(request_type_t::load_sessions, meeting_key);
}

static void on_session_selected(uint32_t meeting_key, uint32_t session_key, void *)
{
    ESP_LOGI(TAG, "Session selected: meeting=%u session=%u",
             static_cast<unsigned>(meeting_key), static_cast<unsigned>(session_key));
    post_request(request_type_t::show_timing, meeting_key, session_key);
}

static void load_meetings()
{
    std::vector<f1::Meeting> meetings;
    esp_err_t err = ESP_FAIL;
    for (int attempt = 0; attempt < 5 && err != ESP_OK; ++attempt) {
        err = openf1::fetch_meetings(s_year, &meetings);
        if (err != ESP_OK && attempt < 4) {
            vTaskDelay(pdMS_TO_TICKS(4000));
        }
    }
    if (err == ESP_OK) {
        session_selector_show_meetings(meetings);
    } else {
        ESP_LOGE(TAG, "Unable to load race calendar: %s", esp_err_to_name(err));
        session_selector_show_status("SCHEDULE UNAVAILABLE");
    }
}

static void load_sessions(uint32_t meeting_key)
{
    std::vector<f1::Session> sessions;
    esp_err_t err = openf1::fetch_sessions(meeting_key, &sessions);
    if (err == ESP_OK) {
        s_sessions = sessions;
        session_selector_show_sessions(meeting_key, sessions);
    } else {
        ESP_LOGE(TAG, "Unable to load sessions: %s", esp_err_to_name(err));
        session_selector_show_status("SESSIONS UNAVAILABLE");
    }
}

// Replay of a past session: a virtual clock starts at the session start and advances in real time.
// Live: the same state and rendering, but the clock is wall time and queries fetch only what changed.
// Live polling stays under the OpenF1 limit (30 req/min): 3 requests per tick plus an occasional refresh.
constexpr int REPLAY_TICK_MS = 5000;
constexpr int LIVE_TICK_MS = 8000;
constexpr int LIVE_REFRESH_S = 30;     // live: stints and qualifying segments change during the session
constexpr int LIVE_PRESTART_S = 600;   // live mode starts this long before the scheduled start
constexpr int LIVE_POSTEND_S = 300;    // and ends this long after the scheduled end
constexpr int REPLAY_WINDOW_S = 10;
constexpr int LAP_LOOKBACK_S = 300; // longer than any lap that could still set a best time

struct replay_t {
    volatile bool active = false;
    bool live = false;
    uint32_t lap = 0;         // leader's current lap
    time_t last_update = 0;   // live: wall time of the last successful update
    time_t last_refresh = 0;  // live: wall time of the last stints/segments refresh
    std::vector<f1::Position> positions; // last known position per driver, kept across ticks
    volatile int64_t t0_us = 0; // esp_timer time when the replay clock started (t = 0, first data received)
    volatile int offset_s = 0;  // user sync offset added to t, adjusted from the header touch area
    volatile bool reset_pending = false;
    volatile int delay_s = 0;   // measured request-to-data latency; queries ask for t + delay
    uint32_t session_key = 0;
    std::string session_name;
    uint32_t total_laps = 0;
    std::vector<f1::Driver> drivers;
    std::vector<f1::Stint> stints;
    time_t start = 0;
    time_t end = 0;
    std::vector<f1::Interval> intervals; // last known gap per driver, kept across ticks
    // Non-race sessions: best lap and latest lap number per driver, kept across ticks.
    struct best_t {
        uint32_t driver_number;
        double best_s;
        uint32_t last_lap;
    };
    std::vector<best_t> bests;
    time_t last_lap_query = 0;
    std::vector<f1::Segment> segments; // qualifying only: Q1, Q2, Q3
    bool hold_flag = false;            // a flag screen owns the display; skip timing redraws
    int segment = -1;                  // segment the table currently belongs to
    // Race control: processed up to rc_when (rc_count messages already applied at that second).
    time_t rc_when = 0;
    size_t rc_count = 0;
    bool red = false;
    bool chequered = false;
    bool sc = false;
    bool vsc = false;
    int sector_yellow[64] = {};  // per sector: 0 clear, 1 yellow, 2 double yellow
    flag_screen_t shown_flag = FLAG_SCREEN_EVENT_TIMING;
};

static replay_t s_replay;

// Race-type sessions are ordered by track position; all others by best lap time.
static bool is_race_session()
{
    return strcasecmp(s_replay.session_name.c_str(), "Race") == 0 ||
           strcasecmp(s_replay.session_name.c_str(), "Sprint") == 0;
}

// Replay time in session UTC: session start plus the time elapsed on the internal clock.
static time_t replay_now()
{
    if (s_replay.live) {
        return time(nullptr) + s_replay.offset_s; // offset_s <= 0: broadcast delay
    }
    return s_replay.start + s_replay.offset_s + static_cast<time_t>((esp_timer_get_time() - s_replay.t0_us) / 1000000);
}

// Session time the data requested now will represent when it arrives.
static time_t replay_query_time()
{
    if (s_replay.live) {
        return replay_now();
    }
    return replay_now() + s_replay.delay_s;
}

// Seconds shown in the header: time left in the current qualifying segment, otherwise in the session.
static uint32_t header_remaining(time_t now)
{
    if (!s_replay.segments.empty()) {
        const f1::Segment *current = &s_replay.segments[0];
        for (const f1::Segment &segment : s_replay.segments) {
            if (segment.start_epoch <= now) {
                current = &segment;
            }
        }
        if (current->end_epoch == 0) {
            return 0;
        }
        // Before a segment starts, show its full length; after it ends, 0 until the next one begins.
        const time_t from = std::max(now, current->start_epoch);
        return static_cast<uint32_t>(current->end_epoch > from ? current->end_epoch - from : 0);
    }
    return static_cast<uint32_t>(s_replay.end > now ? s_replay.end - now : 0);
}

// Header countdown (session end minus replay time), updated every second independent of slow fetches.
static void clock_task(void *)
{
    while (true) {
        vTaskDelay(pdMS_TO_TICKS(1000));
        if (s_replay.active) {
            const time_t now = replay_now();
            flag_display_update_race_time(header_remaining(now));
        }
    }
}

static const f1::Driver *find_driver(uint32_t number)
{
    for (const f1::Driver &driver : s_replay.drivers) {
        if (driver.number == number) {
            return &driver;
        }
    }
    return nullptr;
}

static char tyre_for(uint32_t driver_number, uint32_t lap)
{
    const f1::Stint *best = nullptr;
    for (const f1::Stint &stint : s_replay.stints) {
        if (stint.driver_number == driver_number && stint.lap_start <= std::max<uint32_t>(lap, 1) &&
            (best == nullptr || stint.lap_start > best->lap_start)) {
            best = &stint;
        }
    }
    return best != nullptr && !best->compound.empty() ? best->compound[0] : '\0';
}

static f1_team_t team_for(const f1::Driver &driver)
{
    f1_team_t team = f1_team_from_name(driver.team_name.c_str());
    return team != F1_TEAM_UNKNOWN ? team : f1_team_from_colour(driver.team_colour.c_str());
}

static std::string format_lap_time(double seconds)
{
    const int minutes = static_cast<int>(seconds) / 60;
    char buf[16];
    std::snprintf(buf, sizeof(buf), "%d:%06.3f", minutes % 100, std::min(seconds - minutes * 60, 59.999));
    return buf;
}

// Practice/qualifying style table: ordered by best lap, P1 shows its time, the rest the gap to P1.
static void replay_tick_best_laps()
{
    const time_t now = replay_query_time();

    // Qualifying: a new segment starts a fresh table.
    int segment = -1;
    for (size_t i = 0; i < s_replay.segments.size(); ++i) {
        if (s_replay.segments[i].start_epoch <= now) {
            segment = static_cast<int>(i);
        }
    }
    if (segment >= 0 && segment != s_replay.segment) {
        s_replay.segment = segment;
        s_replay.bests.clear();
        s_replay.last_lap_query = 0;
    }
    const time_t segment_start = segment >= 0 ? s_replay.segments[segment].start_epoch : s_replay.start;
    std::string title = s_replay.session_name;
    if (segment >= 0) {
        title += (strncasecmp(title.c_str(), "Sprint", 6) == 0 ? " - SQ" : " - Q") + std::to_string(segment + 1);
    }

    // Laps still in progress at the previous query are picked up again by the overlapping window.
    const time_t from = std::max(segment_start, s_replay.last_lap_query - LAP_LOOKBACK_S);
    std::vector<f1::LapTime> laps;
    esp_err_t err = openf1::fetch_laps(s_replay.session_key, from, now, &laps);
    if (err != ESP_OK) {
        ESP_LOGW(TAG, "Replay laps failed: %s", esp_err_to_name(err));
        return;
    }
    s_replay.last_lap_query = now;

    for (const f1::LapTime &lap : laps) {
        if (lap.start_epoch + static_cast<time_t>(lap.duration_s) > now) {
            continue; // not finished yet at replay time
        }
        auto it = std::find_if(s_replay.bests.begin(), s_replay.bests.end(),
                               [&](const replay_t::best_t &b) { return b.driver_number == lap.driver_number; });
        if (it == s_replay.bests.end()) {
            s_replay.bests.push_back({lap.driver_number, lap.duration_s, lap.lap_number});
        } else {
            it->best_s = std::min(it->best_s, lap.duration_s);
            it->last_lap = std::max(it->last_lap, lap.lap_number);
        }
    }

    struct entry_t {
        uint32_t driver_number;
        double best_s; // 0 = no time yet
        uint32_t last_lap;
    };
    std::vector<entry_t> entries;
    for (const f1::Driver &driver : s_replay.drivers) {
        entry_t entry = {driver.number, 0, 0};
        for (const replay_t::best_t &b : s_replay.bests) {
            if (b.driver_number == driver.number) {
                entry.best_s = b.best_s;
                entry.last_lap = b.last_lap;
            }
        }
        entries.push_back(entry);
    }
    std::stable_sort(entries.begin(), entries.end(), [](const entry_t &a, const entry_t &b) {
        if ((a.best_s > 0) != (b.best_s > 0)) {
            return a.best_s > 0;
        }
        return a.best_s < b.best_s;
    });

    const size_t count = std::min(entries.size(), EVENT_TIMING_MAX_ROWS);
    std::vector<event_timing_row_t> rows(count);
    std::vector<std::array<char, 4>> codes(count);
    std::vector<std::string> times(count);
    const double leader = entries.empty() ? 0 : entries[0].best_s;
    for (size_t i = 0; i < count; ++i) {
        const f1::Driver *driver = find_driver(entries[i].driver_number);
        codes[i] = {'-', '-', '-', '\0'};
        rows[i].position = static_cast<uint8_t>(i + 1);
        rows[i].team = F1_TEAM_UNKNOWN;
        if (driver != nullptr) {
            std::snprintf(codes[i].data(), codes[i].size(), "%s", driver->code.c_str());
            rows[i].team = team_for(*driver);
        }
        if (entries[i].best_s <= 0) {
            times[i] = "-";
        } else if (i == 0) {
            times[i] = format_lap_time(entries[i].best_s);
        } else {
            char buf[16];
            std::snprintf(buf, sizeof(buf), "+%.3f", entries[i].best_s - leader);
            times[i] = buf;
        }
        rows[i].driver_code = codes[i].data();
        rows[i].interval = times[i].c_str();
        rows[i].tyre = tyre_for(entries[i].driver_number, entries[i].last_lap);
    }
    if (!s_replay.hold_flag) {
        flag_display_show_event_timing(title.c_str(), 0, 0, rows.data(), rows.size());
    }
}

static bool is_qualifying_session()
{
    return strcasecmp(s_replay.session_name.c_str(), "Qualifying") == 0 ||
           strcasecmp(s_replay.session_name.c_str(), "Sprint Qualifying") == 0;
}

// Live only: drivers may not be published before the session starts, and stints/segments grow while it runs.
// Returns false while the session is not ready to show.
static bool live_prepare()
{
    if (s_replay.drivers.empty() && openf1::fetch_drivers(s_replay.session_key, &s_replay.drivers) != ESP_OK) {
        ESP_LOGW(TAG, "Live: drivers not available yet");
        return false;
    }
    const time_t now = time(nullptr);
    if (now - s_replay.last_refresh >= LIVE_REFRESH_S) {
        s_replay.last_refresh = now;
        if (openf1::fetch_stints(s_replay.session_key, &s_replay.stints) != ESP_OK) {
            ESP_LOGW(TAG, "Live: unable to refresh stints");
        }
        if (is_qualifying_session()) {
            openf1::fetch_segments(s_replay.session_key, &s_replay.segments);
        }
    }
    return true;
}

// Applies new race control messages and switches the display to the active flag, or briefly to green on clear.
// Returns true while the flag screens (including the green flash) must not be overwritten by the timing table.
static bool update_flags()
{
    const time_t until = s_replay.live ? replay_now() : replay_query_time();
    std::vector<f1::RaceControl> messages;
    if (openf1::fetch_race_control(s_replay.session_key, s_replay.rc_when, until, &messages) != ESP_OK) {
        return s_replay.shown_flag != FLAG_SCREEN_EVENT_TIMING;
    }
    size_t skip = s_replay.rc_count;
    for (const f1::RaceControl &m : messages) {
        if (m.when == s_replay.rc_when && skip > 0) {
            --skip;
            continue;
        }
        if (m.when != s_replay.rc_when) {
            s_replay.rc_when = m.when;
            s_replay.rc_count = 0;
        }
        ++s_replay.rc_count;
        const bool clear = m.flag == "CLEAR" || m.flag == "GREEN";
        if (m.category == "SafetyCar") {
            const bool virt = m.message.find("VIRTUAL") != std::string::npos || m.message.find("VSC") != std::string::npos;
            bool &state = virt ? s_replay.vsc : s_replay.sc;
            if (m.message.find("DEPLOYED") != std::string::npos) {
                state = true;
            } else if (m.message.find("IN THIS LAP") != std::string::npos ||
                       m.message.find("ENDING") != std::string::npos) {
                state = false;
            }
        } else if (m.scope == "Track") {
            if (m.flag == "RED") {
                s_replay.red = true;
            } else if (m.flag == "CHEQUERED") {
                s_replay.chequered = true;
            } else if (clear) {
                s_replay.red = s_replay.sc = s_replay.vsc = false;
                std::fill(std::begin(s_replay.sector_yellow), std::end(s_replay.sector_yellow), 0);
            } else if (m.flag == "YELLOW" || m.flag == "DOUBLE YELLOW") {
                s_replay.sector_yellow[0] = m.flag == "YELLOW" ? 1 : 2;
            }
        } else if (m.scope == "Sector" && m.sector < 64) {
            s_replay.sector_yellow[m.sector] = m.flag == "YELLOW" ? 1 : m.flag == "DOUBLE YELLOW" ? 2 : 0;
        }
    }

    // Worst active yellow wins: any double yellow, else any yellow.
    int yellow = 0;
    for (size_t i = 0; i < 64; ++i) {
        yellow = std::max(yellow, s_replay.sector_yellow[i]);
    }
    flag_screen_t wanted = FLAG_SCREEN_EVENT_TIMING;
    if (s_replay.red) {
        wanted = FLAG_SCREEN_RED;
    } else if (s_replay.chequered) {
        wanted = FLAG_SCREEN_CHEQUERED;
    } else if (s_replay.sc) {
        wanted = FLAG_SCREEN_SAFETY_CAR;
    } else if (s_replay.vsc) {
        wanted = FLAG_SCREEN_VSC;
    } else if (yellow == 2) {
        wanted = FLAG_SCREEN_DOUBLE_YELLOW;
    } else if (yellow == 1) {
        wanted = FLAG_SCREEN_YELLOW;
    }

    if (wanted == s_replay.shown_flag) {
        return wanted != FLAG_SCREEN_EVENT_TIMING;
    }
    const flag_screen_t previous = s_replay.shown_flag;
    s_replay.shown_flag = wanted;
    switch (wanted) {
    case FLAG_SCREEN_RED: flag_display_show_red(); break;
    case FLAG_SCREEN_SAFETY_CAR: flag_display_show_safety_car(); break;
    case FLAG_SCREEN_VSC: flag_display_show_vsc(); break;
    case FLAG_SCREEN_DOUBLE_YELLOW: flag_display_show_double_yellow(); break;
    case FLAG_SCREEN_YELLOW: flag_display_show_yellow(); break;
    case FLAG_SCREEN_CHEQUERED: // no dedicated screen yet; keep the timing table
        s_replay.shown_flag = FLAG_SCREEN_EVENT_TIMING;
        return false;
    default:
        if (previous != FLAG_SCREEN_EVENT_TIMING) {
            flag_display_show_green(); // reverts to the timing table by itself
            return true;
        }
        break;
    }
    return wanted != FLAG_SCREEN_EVENT_TIMING;
}

static void replay_tick()
{
    if (s_replay.reset_pending) {
        // Time jumped; drop state accumulated from the old timeline.
        s_replay.reset_pending = false;
        s_replay.last_update = 0;
        s_replay.intervals.clear();
        s_replay.positions.clear();
        s_replay.bests.clear();
        s_replay.last_lap_query = 0;
        s_replay.segment = -1;
        s_replay.rc_when = 0;
        s_replay.rc_count = 0;
        s_replay.red = s_replay.chequered = s_replay.sc = s_replay.vsc = false;
        std::fill(std::begin(s_replay.sector_yellow), std::end(s_replay.sector_yellow), 0);
        s_replay.shown_flag = FLAG_SCREEN_EVENT_TIMING;
    }
    if (s_replay.live && !live_prepare()) {
        return;
    }
    s_replay.hold_flag = update_flags();
    if (!is_race_session()) {
        replay_tick_best_laps();
        return;
    }
    f1::TimingSnapshot snapshot;
    esp_err_t err;
    if (s_replay.live) {
        const time_t until = replay_now();
        err = openf1::fetch_live_update(s_replay.session_key, s_replay.last_update, until, &snapshot);
        if (err == ESP_OK) {
            s_replay.last_update = until;
        }
    } else {
        err = openf1::fetch_snapshot(s_replay.session_key, replay_query_time(), REPLAY_WINDOW_S, &snapshot);
    }
    if (err != ESP_OK) {
        ESP_LOGW(TAG, "%s timing update failed: %s", s_replay.live ? "Live" : "Replay", esp_err_to_name(err));
        return;
    }

    // Both modes deliver per-driver rows; merge them into the state kept across ticks.
    for (const f1::Position &fresh : snapshot.positions) {
        auto known = std::find_if(s_replay.positions.begin(), s_replay.positions.end(),
                                  [&](const f1::Position &p) { return p.driver_number == fresh.driver_number; });
        if (known != s_replay.positions.end()) {
            *known = fresh;
        } else {
            s_replay.positions.push_back(fresh);
        }
    }
    std::sort(s_replay.positions.begin(), s_replay.positions.end(),
              [](const f1::Position &a, const f1::Position &b) { return a.position < b.position; });
    // Live deltas carry no lap when the leader's lap row is outside the window; keep the last one.
    if (snapshot.lap > 0 || !s_replay.live) {
        s_replay.lap = snapshot.lap;
    }

    for (const f1::Interval &fresh : snapshot.intervals) {
        auto known = std::find_if(s_replay.intervals.begin(), s_replay.intervals.end(),
                                  [&](const f1::Interval &i) { return i.driver_number == fresh.driver_number; });
        if (known != s_replay.intervals.end()) {
            *known = fresh;
        } else {
            s_replay.intervals.push_back(fresh);
        }
    }

    const size_t count = std::min(s_replay.positions.size(), EVENT_TIMING_MAX_ROWS);
    std::vector<event_timing_row_t> rows(count);
    std::vector<std::array<char, 4>> codes(count);
    std::vector<std::string> gaps(count);
    for (size_t i = 0; i < count; ++i) {
        const f1::Position &position = s_replay.positions[i];
        const f1::Driver *driver = find_driver(position.driver_number);
        codes[i] = {'-', '-', '-', '\0'};
        rows[i].position = static_cast<uint8_t>(position.position);
        rows[i].team = F1_TEAM_UNKNOWN;
        if (driver != nullptr) {
            std::snprintf(codes[i].data(), codes[i].size(), "%s", driver->code.c_str());
            rows[i].team = team_for(*driver);
        }
        gaps[i] = position.position == 1 ? "Interval" : "";
        for (const f1::Interval &interval : s_replay.intervals) {
            if (interval.driver_number == position.driver_number && position.position != 1) {
                gaps[i] = interval.interval;
            }
        }
        rows[i].driver_code = codes[i].data();
        rows[i].interval = gaps[i].c_str();
        rows[i].tyre = tyre_for(position.driver_number, s_replay.lap);
    }
    if (!s_replay.hold_flag) {
        flag_display_show_event_timing(s_replay.session_name.c_str(), s_replay.lap, s_replay.total_laps,
                                       rows.data(), rows.size());
    }
}

// Header touch: shift replay time by delta_s to sync with the broadcast.
void replay_adjust_offset(int delta_s)
{
    if (!s_replay.active) {
        return;
    }
    if (s_replay.live) {
        // Live can only be held back (delay), never ahead of real time; the API has no delay option.
        const int offset = std::min(s_replay.offset_s + delta_s, 0);
        if (offset == s_replay.offset_s) {
            return;
        }
        s_replay.offset_s = offset;
    } else {
        s_replay.offset_s = s_replay.offset_s + delta_s;
    }
    s_replay.reset_pending = true;
    flag_display_update_race_time(header_remaining(replay_now()));
    post_request(request_type_t::refresh_replay, 0);
}

static void show_timing(uint32_t session_key)
{
    const f1::Session *selected = nullptr;
    for (const f1::Session &session : s_sessions) {
        if (session.key == session_key) {
            selected = &session;
        }
    }
    if (selected == nullptr) {
        ESP_LOGW(TAG, "Selected session %u not found", static_cast<unsigned>(session_key));
        return;
    }

    s_replay = replay_t();
    s_replay.session_key = session_key;
    s_replay.session_name = selected->name;
    s_replay.start = selected->start_epoch;
    s_replay.end = selected->end_epoch;
    if (flag_display_show_event_timing(selected->name.c_str(), 0, 0, nullptr, 0) != ESP_OK) {
        return;
    }
    session_selector_set_active(false);

    const time_t now = time(nullptr);
    // Mode decision: sessions in progress (or about to start / just ended) are live, past ones are replayed.
    s_replay.live = now >= selected->start_epoch - LIVE_PRESTART_S && now <= selected->end_epoch + LIVE_POSTEND_S;
    ESP_LOGI(TAG, "Session mode: %s", s_replay.live ? "LIVE" : "REPLAY");
    if (s_replay.live) {
        s_replay.active = true; // live_prepare() loads drivers on the first ticks, retrying until published
        replay_tick();
        flag_display_update_race_time(header_remaining(now));
        return;
    }
    if (selected->start_epoch > now) {
        return; // not started yet, nothing to replay
    }
    if (openf1::fetch_drivers(session_key, &s_replay.drivers) != ESP_OK) {
        ESP_LOGW(TAG, "Unable to load drivers");
        return;
    }
    if (openf1::fetch_stints(session_key, &s_replay.stints) == ESP_OK) {
        for (const f1::Stint &stint : s_replay.stints) {
            s_replay.total_laps = std::max(s_replay.total_laps, stint.lap_end);
        }
    }
    if (is_qualifying_session()) {
        if (openf1::fetch_segments(session_key, &s_replay.segments) != ESP_OK) {
            ESP_LOGW(TAG, "Unable to load qualifying segments");
        }
    }
    // The first request is made at t = 0; the countdown starts once its data arrives, and the
    // measured latency is added to later request times.
    const int64_t request_us = esp_timer_get_time();
    s_replay.t0_us = request_us;
    replay_tick();
    const int64_t arrived_us = esp_timer_get_time();
    s_replay.delay_s = static_cast<int>((arrived_us - request_us) / 1000000);
    s_replay.t0_us = arrived_us;
    flag_display_update_race_time(header_remaining(replay_now()));
    s_replay.active = true;
}

static void data_task(void *)
{
    request_t request = {};
    while (true) {
        const TickType_t wait =
            s_replay.active ? pdMS_TO_TICKS(s_replay.live ? LIVE_TICK_MS : REPLAY_TICK_MS) : portMAX_DELAY;
        if (xQueueReceive(s_requests, &request, wait) != pdPASS) {
            if (s_replay.active) {
                replay_tick(); // queries use the internal clock's current time, however long the last fetch took
            }
            continue;
        }
        switch (request.type) {
        case request_type_t::load_meetings:
            s_replay.active = false;
            load_meetings();
            break;
        case request_type_t::load_sessions:
            s_replay.active = false;
            load_sessions(request.meeting_key);
            break;
        case request_type_t::show_timing:
            show_timing(request.session_key);
            break;
        case request_type_t::refresh_replay:
            if (s_replay.active) {
                replay_tick();
            }
            break;
        }
    }
}

extern "C" void app_main(void)
{
    bsp_board_print_info();

    esp_err_t err = board_setup_init();
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "BSP initialization failed: %s", esp_err_to_name(err));
        return;
    }

    openf1::set_credentials(OPENF1_LOGIN, OPENF1_PASSWORD);
    s_year = app_current_year();

    s_requests = xQueueCreate(4, sizeof(request_t));
    if (s_requests == nullptr) {
        ESP_LOGE(TAG, "Failed to create request queue");
        return;
    }

    session_selector_callbacks_t callbacks;
    callbacks.on_meeting_selected = on_meeting_selected;
    callbacks.on_session_selected = on_session_selected;
    err = session_selector_init(s_year, callbacks);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Session selector init failed: %s", esp_err_to_name(err));
        return;
    }

    err = flag_display_init();
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Flag display init failed: %s", esp_err_to_name(err));
        return;
    }

    if (xTaskCreate(data_task, "data_task", 10240, nullptr, 3, nullptr) != pdPASS ||
        xTaskCreate(clock_task, "clock_task", 3072, nullptr, 2, nullptr) != pdPASS ||
        touch_input_start() != ESP_OK) {
        ESP_LOGE(TAG, "Failed to create tasks");
        return;
    }
    post_request(request_type_t::load_meetings, 0);
}
