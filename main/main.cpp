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
constexpr int REPLAY_TICK_MS = 5000;
constexpr int REPLAY_WINDOW_S = 10;
constexpr int LAP_LOOKBACK_S = 300; // longer than any lap that could still set a best time

struct replay_t {
    volatile bool active = false;
    volatile int64_t t0_us = 0; // esp_timer time when the replay clock started (t = 0)
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
    int segment = -1;                  // segment the table currently belongs to
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
    return s_replay.start + static_cast<time_t>((esp_timer_get_time() - s_replay.t0_us) / 1000000);
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
    const time_t now = replay_now();

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
    flag_display_show_event_timing(title.c_str(), 0, 0, rows.data(), rows.size());
}

static void replay_tick()
{
    if (!is_race_session()) {
        replay_tick_best_laps();
        return;
    }
    f1::TimingSnapshot snapshot;
    esp_err_t err = openf1::fetch_snapshot(s_replay.session_key, replay_now(), REPLAY_WINDOW_S, &snapshot);
    if (err != ESP_OK) {
        ESP_LOGW(TAG, "Replay snapshot failed: %s", esp_err_to_name(err));
        return;
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

    const size_t count = std::min(snapshot.positions.size(), EVENT_TIMING_MAX_ROWS);
    std::vector<event_timing_row_t> rows(count);
    std::vector<std::array<char, 4>> codes(count);
    std::vector<std::string> gaps(count);
    for (size_t i = 0; i < count; ++i) {
        const f1::Position &position = snapshot.positions[i];
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
        rows[i].tyre = tyre_for(position.driver_number, snapshot.lap);
    }
    flag_display_show_event_timing(s_replay.session_name.c_str(), snapshot.lap, s_replay.total_laps,
                                   rows.data(), rows.size());
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

    if (selected->start_epoch > time(nullptr)) {
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
    if (strcasecmp(selected->name.c_str(), "Qualifying") == 0 ||
        strcasecmp(selected->name.c_str(), "Sprint Qualifying") == 0) {
        if (openf1::fetch_segments(session_key, &s_replay.segments) != ESP_OK) {
            ESP_LOGW(TAG, "Unable to load qualifying segments");
        }
    }
    s_replay.t0_us = esp_timer_get_time();
    s_replay.active = true;
    replay_tick();
}

static void data_task(void *)
{
    request_t request = {};
    while (true) {
        const TickType_t wait = s_replay.active ? pdMS_TO_TICKS(REPLAY_TICK_MS) : portMAX_DELAY;
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
