#include "openf1.h"

#include "cJSON.h"
#include "esp_crt_bundle.h"
#include "esp_http_client.h"
#include "esp_heap_caps.h"
#include "esp_log.h"

#include <algorithm>
#include <cctype>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>

namespace openf1 {
namespace {

constexpr char BASE_URL[] = "https://api.openf1.org/v1";
constexpr char TOKEN_URL[] = "https://api.openf1.org/token";
constexpr int TOKEN_EXPIRY_MARGIN_S = 60;
constexpr int LIVE_OVERLAP_S = 15;
constexpr int LIVE_FIRST_INTERVAL_WINDOW_S = 30;
constexpr int LIVE_LAP_WINDOW_S = 5 * 60; // longer than a safety-car lap, to find the leader's current lap
constexpr int DEFAULT_TOKEN_LIFETIME_S = 60 * 60;
static const char *TAG = "openf1";

std::string s_login;
std::string s_password;
std::string s_token;
time_t s_token_expiry = 0;

time_t parse_utc_time(const std::string &value)
{
    int year = 0, month = 0, day = 0, hour = 0, minute = 0, second = 0;
    if (std::sscanf(value.c_str(), "%4d-%2d-%2dT%2d:%2d:%2d",
                    &year, &month, &day, &hour, &minute, &second) != 6) {
        return 0;
    }
    struct tm calendar = {};
    calendar.tm_year = year - 1900;
    calendar.tm_mon = month - 1;
    calendar.tm_mday = day;
    calendar.tm_hour = hour;
    calendar.tm_min = minute;
    calendar.tm_sec = second;
    return timegm(&calendar);
}

std::string json_string(const cJSON *object, const char *name)
{
    const cJSON *item = cJSON_GetObjectItemCaseSensitive(object, name);
    return cJSON_IsString(item) && item->valuestring != nullptr ? item->valuestring : "";
}

uint32_t json_uint(const cJSON *object, const char *name)
{
    const cJSON *item = cJSON_GetObjectItemCaseSensitive(object, name);
    return cJSON_IsNumber(item) && item->valuedouble > 0 ? static_cast<uint32_t>(item->valuedouble) : 0U;
}

bool json_bool(const cJSON *object, const char *name)
{
    return cJSON_IsTrue(cJSON_GetObjectItemCaseSensitive(object, name));
}

std::string url_encode(const std::string &value)
{
    std::string out;
    char buf[4];
    for (unsigned char c : value) {
        if (std::isalnum(c) || c == '-' || c == '_' || c == '.' || c == '~') {
            out += static_cast<char>(c);
        } else {
            std::snprintf(buf, sizeof(buf), "%%%02X", c);
            out += buf;
        }
    }
    return out;
}

esp_err_t http_event_handler(esp_http_client_event_t *event)
{
    if (event->event_id == HTTP_EVENT_ON_DATA && event->data != nullptr && event->user_data != nullptr) {
        static_cast<std::string *>(event->user_data)->append(static_cast<const char *>(event->data), event->data_len);
    }
    return ESP_OK;
}

esp_err_t http_request(const std::string &url, const char *post_body, const std::string &bearer,
                       std::string *response, int *status_out)
{
    esp_http_client_config_t config = {};
    config.url = url.c_str();
    config.timeout_ms = 12000;
    config.buffer_size = 4096;
    config.buffer_size_tx = 2048;
    config.event_handler = http_event_handler;
    config.user_data = response;
    config.crt_bundle_attach = esp_crt_bundle_attach;
    config.method = post_body != nullptr ? HTTP_METHOD_POST : HTTP_METHOD_GET;

    esp_http_client_handle_t client = esp_http_client_init(&config);
    if (client == nullptr) {
        return ESP_ERR_NO_MEM;
    }
    esp_http_client_set_header(client, "Accept", "application/json");
    if (post_body != nullptr) {
        esp_http_client_set_header(client, "Content-Type", "application/x-www-form-urlencoded");
        esp_http_client_set_post_field(client, post_body, static_cast<int>(std::strlen(post_body)));
    }
    if (!bearer.empty()) {
        esp_http_client_set_header(client, "Authorization", ("Bearer " + bearer).c_str());
    }
    esp_err_t err = esp_http_client_perform(client);
    *status_out = esp_http_client_get_status_code(client);
    esp_http_client_cleanup(client);
    return err;
}

esp_err_t refresh_token()
{
    s_token.clear();
    if (s_login.empty() || s_password.empty()) {
        return ESP_ERR_INVALID_STATE;
    }

    const std::string body = "username=" + url_encode(s_login) + "&password=" + url_encode(s_password);
    std::string response;
    int status = 0;
    esp_err_t err = http_request(TOKEN_URL, body.c_str(), "", &response, &status);
    if (err != ESP_OK) {
        return err;
    }
    if (status < 200 || status >= 300) {
        ESP_LOGW(TAG, "Token request rejected (HTTP %d)", status);
        return ESP_FAIL;
    }

    cJSON *root = cJSON_Parse(response.c_str());
    const std::string token = root != nullptr ? json_string(root, "access_token") : "";
    int lifetime = DEFAULT_TOKEN_LIFETIME_S;
    if (root != nullptr) {
        const cJSON *expires = cJSON_GetObjectItemCaseSensitive(root, "expires_in");
        if (cJSON_IsNumber(expires) && expires->valueint > 0) {
            lifetime = expires->valueint;
        } else if (cJSON_IsString(expires) && std::atoi(expires->valuestring) > 0) {
            lifetime = std::atoi(expires->valuestring);
        }
    }
    cJSON_Delete(root);
    if (token.empty()) {
        return ESP_ERR_INVALID_RESPONSE;
    }
    s_token = token;
    s_token_expiry = time(nullptr) + std::max(lifetime - TOKEN_EXPIRY_MARGIN_S, 0);
    return ESP_OK;
}

bool token_valid()
{
    return !s_token.empty() && time(nullptr) < s_token_expiry;
}

esp_err_t get_json(const std::string &url, std::string *response)
{
    if (!token_valid() && !s_login.empty()) {
        esp_err_t token_err = refresh_token();
        if (token_err != ESP_OK) {
            ESP_LOGW(TAG, "Token unavailable (%s), trying unauthenticated", esp_err_to_name(token_err));
        }
    }

    for (int attempt = 0; attempt < 2; ++attempt) {
        response->clear();
        int status = 0;
        esp_err_t err = http_request(url, nullptr, s_token, response, &status);
        if (err != ESP_OK) {
            return err;
        }
        if (status == 401 && attempt == 0 && !s_login.empty() && refresh_token() == ESP_OK) {
            continue;
        }
        if (status == 404) {
            return ESP_ERR_NOT_FOUND;
        }
        return status >= 200 && status < 300 ? ESP_OK : ESP_FAIL;
    }
    return ESP_FAIL;
}

}  // namespace

void init_json_allocator()
{
    // Large responses create thousands of small nodes; keep them out of scarce internal RAM.
    static cJSON_Hooks hooks = {
        [](size_t size) -> void * {
            void *p = heap_caps_malloc(size, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
            return p != nullptr ? p : heap_caps_malloc(size, MALLOC_CAP_8BIT);
        },
        [](void *p) { heap_caps_free(p); },
    };
    cJSON_InitHooks(&hooks);
}

void set_credentials(const char *login, const char *password)
{
    init_json_allocator();
    s_login = login != nullptr ? login : "";
    s_password = password != nullptr ? password : "";
    s_token.clear();
    s_token_expiry = 0;
}

esp_err_t fetch_meetings(int year, std::vector<f1::Meeting> *meetings)
{
    if (meetings == nullptr) {
        return ESP_ERR_INVALID_ARG;
    }
    char url[96] = {};
    std::snprintf(url, sizeof(url), "%s/meetings?year=%d", BASE_URL, year);
    std::string response;
    esp_err_t err = get_json(url, &response);
    if (err != ESP_OK) {
        return err;
    }

    cJSON *root = cJSON_Parse(response.c_str());
    if (root == nullptr || !cJSON_IsArray(root)) {
        cJSON_Delete(root);
        return ESP_ERR_INVALID_RESPONSE;
    }

    std::vector<f1::Meeting> parsed;
    const cJSON *item = nullptr;
    cJSON_ArrayForEach(item, root) {
        f1::Meeting meeting;
        meeting.key = json_uint(item, "meeting_key");
        meeting.name = json_string(item, "meeting_name");
        meeting.location = json_string(item, "location");
        meeting.cancelled = json_bool(item, "is_cancelled");
        meeting.start_epoch = parse_utc_time(json_string(item, "date_start"));
        meeting.end_epoch = parse_utc_time(json_string(item, "date_end"));
        if (meeting.key != 0 && meeting.name.find("Grand Prix") != std::string::npos) {
            parsed.push_back(std::move(meeting));
        }
    }
    cJSON_Delete(root);

    std::sort(parsed.begin(), parsed.end(), [](const f1::Meeting &a, const f1::Meeting &b) {
        return a.start_epoch < b.start_epoch;
    });
    *meetings = std::move(parsed);
    return meetings->empty() ? ESP_ERR_NOT_FOUND : ESP_OK;
}

esp_err_t fetch_sessions(uint32_t meeting_key, std::vector<f1::Session> *sessions)
{
    if (sessions == nullptr) {
        return ESP_ERR_INVALID_ARG;
    }
    char url[112] = {};
    std::snprintf(url, sizeof(url), "%s/sessions?meeting_key=%u", BASE_URL, static_cast<unsigned>(meeting_key));
    std::string response;
    esp_err_t err = get_json(url, &response);
    if (err != ESP_OK) {
        return err;
    }

    cJSON *root = cJSON_Parse(response.c_str());
    if (root == nullptr || !cJSON_IsArray(root)) {
        cJSON_Delete(root);
        return ESP_ERR_INVALID_RESPONSE;
    }

    std::vector<f1::Session> parsed;
    const cJSON *item = nullptr;
    cJSON_ArrayForEach(item, root) {
        f1::Session session;
        session.key = json_uint(item, "session_key");
        session.meeting_key = json_uint(item, "meeting_key");
        session.name = json_string(item, "session_name");
        session.cancelled = json_bool(item, "is_cancelled");
        session.start_epoch = parse_utc_time(json_string(item, "date_start"));
        session.end_epoch = parse_utc_time(json_string(item, "date_end"));
        if (session.key != 0 && session.meeting_key == meeting_key) {
            parsed.push_back(std::move(session));
        }
    }
    cJSON_Delete(root);

    std::sort(parsed.begin(), parsed.end(), [](const f1::Session &a, const f1::Session &b) {
        return a.start_epoch < b.start_epoch;
    });
    *sessions = std::move(parsed);
    return sessions->empty() ? ESP_ERR_NOT_FOUND : ESP_OK;
}

namespace {

std::string format_utc_time(time_t epoch)
{
    struct tm calendar = {};
    gmtime_r(&epoch, &calendar);
    char buf[24] = {};
    std::strftime(buf, sizeof(buf), "%Y-%m-%dT%H:%M:%S", &calendar);
    return buf;
}

// Gap values are a number, a string such as "+1 LAP", or null.
std::string json_gap(const cJSON *object, const char *name)
{
    const cJSON *item = cJSON_GetObjectItemCaseSensitive(object, name);
    if (cJSON_IsNumber(item)) {
        char buf[16];
        std::snprintf(buf, sizeof(buf), "+%.3f", item->valuedouble);
        return buf;
    }
    return cJSON_IsString(item) && item->valuestring != nullptr ? item->valuestring : "";
}

// Fetches `url` and returns the parsed JSON array (caller frees), or nullptr with err set.
cJSON *get_array(const std::string &url, esp_err_t *err)
{
    std::string response;
    *err = get_json(url, &response);
    if (*err == ESP_ERR_NOT_FOUND) {
        // OpenF1 answers 404 "No results found." for an empty query.
        response = "[]";
        *err = ESP_OK;
    }
    if (*err != ESP_OK) {
        return nullptr;
    }
    cJSON *root = cJSON_Parse(response.c_str());
    if (root == nullptr || !cJSON_IsArray(root)) {
        cJSON_Delete(root);
        *err = ESP_ERR_INVALID_RESPONSE;
        return nullptr;
    }
    return root;
}

std::string session_url(const char *endpoint, uint32_t session_key)
{
    return std::string(BASE_URL) + "/" + endpoint + "?session_key=" + std::to_string(session_key);
}

std::string window_filter(time_t at_epoch, int window_s)
{
    return "&date%3E=" + format_utc_time(at_epoch - window_s) + "&date%3C=" + format_utc_time(at_epoch);
}

}  // namespace

esp_err_t fetch_drivers(uint32_t session_key, std::vector<f1::Driver> *drivers)
{
    if (drivers == nullptr) {
        return ESP_ERR_INVALID_ARG;
    }
    esp_err_t err = ESP_OK;
    cJSON *root = get_array(session_url("drivers", session_key), &err);
    if (root == nullptr) {
        return err;
    }
    drivers->clear();
    const cJSON *item = nullptr;
    cJSON_ArrayForEach(item, root) {
        f1::Driver driver;
        driver.number = json_uint(item, "driver_number");
        driver.code = json_string(item, "name_acronym");
        driver.team_name = json_string(item, "team_name");
        driver.team_colour = json_string(item, "team_colour");
        if (driver.number != 0) {
            drivers->push_back(std::move(driver));
        }
    }
    cJSON_Delete(root);
    return drivers->empty() ? ESP_ERR_NOT_FOUND : ESP_OK;
}

esp_err_t fetch_stints(uint32_t session_key, std::vector<f1::Stint> *stints)
{
    if (stints == nullptr) {
        return ESP_ERR_INVALID_ARG;
    }
    esp_err_t err = ESP_OK;
    cJSON *root = get_array(session_url("stints", session_key), &err);
    if (root == nullptr) {
        return err;
    }
    stints->clear();
    const cJSON *item = nullptr;
    cJSON_ArrayForEach(item, root) {
        f1::Stint stint;
        stint.driver_number = json_uint(item, "driver_number");
        stint.compound = json_string(item, "compound");
        stint.lap_start = json_uint(item, "lap_start");
        stint.lap_end = json_uint(item, "lap_end");
        if (stint.driver_number != 0) {
            stints->push_back(std::move(stint));
        }
    }
    cJSON_Delete(root);
    return ESP_OK;
}

namespace {

// Shared by replay and live: the callers differ only in the time filters appended to each query.
// Samples are returned in chronological order, so later entries overwrite earlier ones.
esp_err_t fetch_timing(uint32_t session_key, const std::string &position_filter, const std::string &interval_filter,
                       const std::string &lap_filter, f1::TimingSnapshot *snapshot)
{
    f1::TimingSnapshot result;
    esp_err_t err = ESP_OK;

    // Position rows are only emitted on change (~300 per race).
    cJSON *positions = get_array(session_url("position", session_key) + position_filter, &err);
    if (positions == nullptr) {
        return err;
    }
    const cJSON *item = nullptr;
    cJSON_ArrayForEach(item, positions) {
        const uint32_t number = json_uint(item, "driver_number");
        const uint32_t position = json_uint(item, "position");
        if (number == 0 || position == 0) {
            continue;
        }
        auto found = std::find_if(result.positions.begin(), result.positions.end(),
                                  [number](const f1::Position &p) { return p.driver_number == number; });
        if (found != result.positions.end()) {
            found->position = position;
        } else {
            result.positions.push_back({number, position});
        }
    }
    cJSON_Delete(positions);
    std::sort(result.positions.begin(), result.positions.end(),
              [](const f1::Position &a, const f1::Position &b) { return a.position < b.position; });

    cJSON *intervals = get_array(session_url("intervals", session_key) + interval_filter, &err);
    if (intervals != nullptr) {
        cJSON_ArrayForEach(item, intervals) {
            const uint32_t number = json_uint(item, "driver_number");
            if (number == 0) {
                continue;
            }
            f1::Interval interval;
            interval.driver_number = number;
            interval.interval = json_gap(item, "interval");
            interval.gap_to_leader = json_gap(item, "gap_to_leader");
            auto found = std::find_if(result.intervals.begin(), result.intervals.end(),
                                      [number](const f1::Interval &i) { return i.driver_number == number; });
            if (found != result.intervals.end()) {
                *found = std::move(interval);
            } else {
                result.intervals.push_back(std::move(interval));
            }
        }
        cJSON_Delete(intervals);
    }

    // Leader's current lap: latest lap started at or before the instant.
    cJSON *laps = get_array(session_url("laps", session_key) + lap_filter, &err);
    if (laps != nullptr) {
        cJSON_ArrayForEach(item, laps) {
            result.lap = std::max(result.lap, json_uint(item, "lap_number"));
        }
        cJSON_Delete(laps);
    }

    *snapshot = std::move(result);
    return ESP_OK;
}

}  // namespace

esp_err_t fetch_snapshot(uint32_t session_key, time_t at_epoch, int window_s, f1::TimingSnapshot *snapshot)
{
    if (snapshot == nullptr) {
        return ESP_ERR_INVALID_ARG;
    }
    return fetch_timing(session_key, "&date%3C=" + format_utc_time(at_epoch), window_filter(at_epoch, window_s),
                        "&date_start%3E=" + format_utc_time(at_epoch - 120) + "&date_start%3C=" + format_utc_time(at_epoch),
                        snapshot);
}

esp_err_t fetch_live_update(uint32_t session_key, time_t since_epoch, time_t until_epoch, f1::TimingSnapshot *update)
{
    if (update == nullptr) {
        return ESP_ERR_INVALID_ARG;
    }
    // Overlap with the previous poll: rows can be ingested slightly after their timestamp.
    // Re-reading them is harmless because the caller merges by driver.
    const time_t from = since_epoch > 0 ? since_epoch - LIVE_OVERLAP_S : 0;
    // Rows after until_epoch are excluded so a user-set broadcast delay holds the display back.
    const std::string until = "&date%3C=" + format_utc_time(until_epoch);
    const std::string position_filter = (from > 0 ? "&date%3E=" + format_utc_time(from) : "") + until;
    const time_t interval_from = from > 0 ? from : until_epoch - LIVE_FIRST_INTERVAL_WINDOW_S;
    return fetch_timing(session_key, position_filter, "&date%3E=" + format_utc_time(interval_from) + until,
                        "&date_start%3E=" + format_utc_time(until_epoch - LIVE_LAP_WINDOW_S) +
                            "&date_start%3C=" + format_utc_time(until_epoch),
                        update);
}

esp_err_t fetch_laps(uint32_t session_key, time_t from_epoch, time_t to_epoch, std::vector<f1::LapTime> *laps)
{
    if (laps == nullptr) {
        return ESP_ERR_INVALID_ARG;
    }
    esp_err_t err = ESP_OK;
    cJSON *root = get_array(session_url("laps", session_key) + "&date_start%3E=" + format_utc_time(from_epoch) +
                                "&date_start%3C=" + format_utc_time(to_epoch),
                            &err);
    if (root == nullptr) {
        return err;
    }
    laps->clear();
    const cJSON *item = nullptr;
    cJSON_ArrayForEach(item, root) {
        const cJSON *duration = cJSON_GetObjectItemCaseSensitive(item, "lap_duration");
        if (!cJSON_IsNumber(duration) || duration->valuedouble <= 0) {
            continue;
        }
        f1::LapTime lap;
        lap.driver_number = json_uint(item, "driver_number");
        lap.lap_number = json_uint(item, "lap_number");
        lap.start_epoch = parse_utc_time(json_string(item, "date_start"));
        lap.duration_s = duration->valuedouble;
        if (lap.driver_number != 0) {
            laps->push_back(lap);
        }
    }
    cJSON_Delete(root);
    return ESP_OK;
}

esp_err_t fetch_segments(uint32_t session_key, std::vector<f1::Segment> *segments)
{
    if (segments == nullptr) {
        return ESP_ERR_INVALID_ARG;
    }
    esp_err_t err = ESP_OK;
    cJSON *root = get_array(session_url("race_control", session_key) + "&category=SessionStatus", &err);
    if (root == nullptr) {
        return err;
    }
    segments->clear();
    const cJSON *item = nullptr;
    cJSON_ArrayForEach(item, root) {
        const std::string message = json_string(item, "message");
        const time_t when = parse_utc_time(json_string(item, "date"));
        if (message == "SESSION STARTED") {
            f1::Segment segment;
            segment.start_epoch = when;
            segments->push_back(segment);
        } else if (message == "SESSION FINISHED" && !segments->empty() && segments->back().end_epoch == 0) {
            segments->back().end_epoch = when;
        }
    }
    cJSON_Delete(root);
    return ESP_OK;
}

esp_err_t fetch_race_control(uint32_t session_key, time_t from_epoch, time_t until_epoch,
                            std::vector<f1::RaceControl> *messages)
{
    if (messages == nullptr) {
        return ESP_ERR_INVALID_ARG;
    }
    esp_err_t err = ESP_OK;
    std::string url = session_url("race_control", session_key) + "&date%3C=" + format_utc_time(until_epoch);
    if (from_epoch > 0) {
        url += "&date%3E=" + format_utc_time(from_epoch);
    }
    cJSON *root = get_array(url, &err);
    if (root == nullptr) {
        return err;
    }
    messages->clear();
    const cJSON *item = nullptr;
    cJSON_ArrayForEach(item, root) {
        f1::RaceControl entry;
        entry.category = json_string(item, "category");
        if (entry.category != "Flag" && entry.category != "SafetyCar" && entry.category != "SessionStatus") {
            continue;
        }
        entry.when = parse_utc_time(json_string(item, "date"));
        entry.flag = json_string(item, "flag");
        entry.scope = json_string(item, "scope");
        entry.message = json_string(item, "message");
        entry.sector = json_uint(item, "sector");
        messages->push_back(std::move(entry));
    }
    cJSON_Delete(root);
    return ESP_OK;
}

}  // namespace openf1
