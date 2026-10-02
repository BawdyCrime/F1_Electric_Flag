#include "session_selector.h"

#include "bsp_display.h"
#include "screen_header.h"
#include "esp_crt_bundle.h"
#include "esp_http_client.h"
#include "esp_lvgl_port.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "lvgl.h"

#include "cJSON.h"

#include <algorithm>
#include <cstdio>
#include <cstdlib>
#include <ctime>
#include <new>
#include <string>
#include <vector>

namespace {

constexpr int32_t RACE_ROW_HEIGHT = 62;
constexpr int32_t SESSION_ROW_HEIGHT = 58;
constexpr int32_t SESSION_LIST_TOP = 136;
constexpr int32_t SESSION_LIST_BOTTOM_MARGIN = 52;
constexpr int32_t BACK_TOUCH_TARGET_WIDTH = 120;
constexpr int32_t BACK_TOUCH_TARGET_HEIGHT = 48;
constexpr char OPENF1_BASE_URL[] = "https://api.openf1.org/v1";
static const char *TAG = "session_selector";

struct meeting_t {
    uint32_t key;
    std::string name;
    std::string location;
    std::string date_start;
    std::string date_end;
    bool cancelled;
    time_t start_epoch;
    time_t end_epoch;
};

struct session_t {
    uint32_t key;
    uint32_t meeting_key;
    std::string name;
    std::string date_start;
    bool cancelled;
    time_t start_epoch;
};

enum class page_t : uint8_t {
    races,
    sessions,
};

struct selector_state_t {
    lv_obj_t *screen = nullptr;
    lv_obj_t *title = nullptr;
    lv_obj_t *subtitle = nullptr;
    lv_obj_t *back = nullptr;
    lv_obj_t *list = nullptr;
    lv_obj_t *status = nullptr;
    std::vector<meeting_t> meetings;
    std::vector<session_t> sessions;
    page_t page = page_t::races;
    int selected_meeting = -1;
    int selected_session = -1;
    int year = 0;
    bool initialized = false;
    bool pointer_down = false;
    bool pointer_dragged = false;
    int pointer_start_x = 0;
    int pointer_start_y = 0;
    int pointer_last_y = 0;
    bool pointer_started_in_list = false;
};

static selector_state_t s_state;

int get_current_year()
{
    time_t now = time(nullptr);
    if (now > 1700000000) {
        struct tm calendar = {};
        gmtime_r(&now, &calendar);
        return calendar.tm_year + 1900;
    }

    char build_year[5] = {};
    std::snprintf(build_year, sizeof(build_year), "%.4s", __DATE__ + 7);
    return std::atoi(build_year);
}

time_t parse_utc_time(const std::string &value)
{
    int year = 0;
    int month = 0;
    int day = 0;
    int hour = 0;
    int minute = 0;
    int second = 0;
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
    return cJSON_IsNumber(item) && item->valuedouble > 0
               ? static_cast<uint32_t>(item->valuedouble)
               : 0U;
}

bool json_bool(const cJSON *object, const char *name)
{
    const cJSON *item = cJSON_GetObjectItemCaseSensitive(object, name);
    return cJSON_IsTrue(item);
}

esp_err_t http_event_handler(esp_http_client_event_t *event)
{
    if (event->event_id == HTTP_EVENT_ON_DATA && event->data != nullptr && event->user_data != nullptr) {
        auto *response = static_cast<std::string *>(event->user_data);
        response->append(static_cast<const char *>(event->data), event->data_len);
    }
    return ESP_OK;
}

esp_err_t get_json(const std::string &url, std::string *response)
{
    if (response == nullptr) {
        return ESP_ERR_INVALID_ARG;
    }

    esp_http_client_config_t config = {};
    config.url = url.c_str();
    config.timeout_ms = 12000;
    config.buffer_size = 4096;
    config.event_handler = http_event_handler;
    config.user_data = response;
    config.crt_bundle_attach = esp_crt_bundle_attach;

    esp_http_client_handle_t client = esp_http_client_init(&config);
    if (client == nullptr) {
        return ESP_ERR_NO_MEM;
    }
    esp_http_client_set_header(client, "Accept", "application/json");
    esp_err_t err = esp_http_client_perform(client);
    const int status = esp_http_client_get_status_code(client);
    esp_http_client_cleanup(client);

    if (err != ESP_OK) {
        return err;
    }
    return status >= 200 && status < 300 ? ESP_OK : ESP_FAIL;
}

bool is_grand_prix(const std::string &name)
{
    return name.find("Grand Prix") != std::string::npos;
}

esp_err_t parse_meetings(const std::string &response, std::vector<meeting_t> *meetings)
{
    cJSON *root = cJSON_Parse(response.c_str());
    if (root == nullptr || !cJSON_IsArray(root)) {
        cJSON_Delete(root);
        return ESP_ERR_INVALID_RESPONSE;
    }

    std::vector<meeting_t> parsed;
    const cJSON *item = nullptr;
    cJSON_ArrayForEach(item, root) {
        meeting_t meeting = {};
        meeting.key = json_uint(item, "meeting_key");
        meeting.name = json_string(item, "meeting_name");
        meeting.location = json_string(item, "location");
        meeting.date_start = json_string(item, "date_start");
        meeting.date_end = json_string(item, "date_end");
        meeting.cancelled = json_bool(item, "is_cancelled");
        meeting.start_epoch = parse_utc_time(meeting.date_start);
        meeting.end_epoch = parse_utc_time(meeting.date_end);
        if (meeting.key != 0 && is_grand_prix(meeting.name)) {
            parsed.push_back(std::move(meeting));
        }
    }
    cJSON_Delete(root);

    std::sort(parsed.begin(), parsed.end(), [](const meeting_t &left, const meeting_t &right) {
        return left.start_epoch < right.start_epoch;
    });
    *meetings = std::move(parsed);
    return meetings->empty() ? ESP_ERR_NOT_FOUND : ESP_OK;
}

esp_err_t parse_sessions(const std::string &response, uint32_t meeting_key,
                         std::vector<session_t> *sessions)
{
    cJSON *root = cJSON_Parse(response.c_str());
    if (root == nullptr || !cJSON_IsArray(root)) {
        cJSON_Delete(root);
        return ESP_ERR_INVALID_RESPONSE;
    }

    std::vector<session_t> parsed;
    const cJSON *item = nullptr;
    cJSON_ArrayForEach(item, root) {
        session_t session = {};
        session.key = json_uint(item, "session_key");
        session.meeting_key = json_uint(item, "meeting_key");
        session.name = json_string(item, "session_name");
        session.date_start = json_string(item, "date_start");
        session.cancelled = json_bool(item, "is_cancelled");
        session.start_epoch = parse_utc_time(session.date_start);
        if (session.key != 0 && session.meeting_key == meeting_key) {
            parsed.push_back(std::move(session));
        }
    }
    cJSON_Delete(root);

    std::sort(parsed.begin(), parsed.end(), [](const session_t &left, const session_t &right) {
        return left.start_epoch < right.start_epoch;
    });
    *sessions = std::move(parsed);
    return sessions->empty() ? ESP_ERR_NOT_FOUND : ESP_OK;
}

void style_label(lv_obj_t *label, lv_color_t color, const lv_font_t *font)
{
    lv_obj_set_style_text_color(label, color, 0);
    lv_obj_set_style_text_font(label, font, 0);
}

lv_obj_t *create_row(lv_obj_t *parent, int32_t height, bool highlighted)
{
    lv_obj_t *row = lv_obj_create(parent);
    lv_obj_set_size(row, BSP_DISPLAY_PANEL_WIDTH - 24, height);
    lv_obj_set_style_bg_color(row, highlighted ? lv_color_hex(0x202020) : lv_color_black(), 0);
    lv_obj_set_style_bg_opa(row, LV_OPA_COVER, 0);
    lv_obj_set_style_border_width(row, 0, 0);
    lv_obj_set_style_border_side(row, LV_BORDER_SIDE_BOTTOM, 0);
    lv_obj_set_style_border_color(row, lv_color_hex(0x303030), 0);
    lv_obj_set_style_radius(row, 0, 0);
    lv_obj_set_style_pad_all(row, 0, 0);
    lv_obj_set_scrollable(row, false);
    return row;
}

int find_center_meeting_index()
{
    const time_t now = time(nullptr);
    int next_index = -1;
    for (size_t index = 0; index < s_state.meetings.size(); ++index) {
        const meeting_t &meeting = s_state.meetings[index];
        if (meeting.cancelled) {
            continue;
        }
        if (meeting.start_epoch <= now && meeting.end_epoch >= now) {
            return static_cast<int>(index);
        }
        if (meeting.start_epoch >= now && next_index < 0) {
            next_index = static_cast<int>(index);
        }
    }
    return next_index;
}

std::string format_local_date(const std::string &date)
{
    const time_t utc = parse_utc_time(date);
    if (utc == 0) {
        return "DATE UNAVAILABLE";
    }

    struct tm calendar = {};
    if (localtime_r(&utc, &calendar) == nullptr) {
        return "DATE UNAVAILABLE";
    }

    char result[40] = {};
    std::strftime(result, sizeof(result), "%a %d %b  %H:%M", &calendar);
    return result;
}

void set_status_locked(const char *text)
{
    lv_obj_clean(s_state.list);
    lv_label_set_text(s_state.status, text);
    lv_obj_set_hidden(s_state.status, false);
}

void clear_status_locked()
{
    lv_obj_set_hidden(s_state.status, true);
}

void render_races_locked()
{
    s_state.page = page_t::races;
    s_state.selected_session = -1;
    lv_label_set_text_fmt(s_state.title, "%d RACE CALENDAR", s_state.year);
    lv_label_set_text(s_state.subtitle, "GRAND PRIX");
    lv_obj_set_hidden(s_state.back, true);
    lv_obj_set_pos(s_state.list, 12, 116);
    lv_obj_set_size(s_state.list, BSP_DISPLAY_PANEL_WIDTH - 24, BSP_DISPLAY_PANEL_HEIGHT - 128);
    lv_obj_clean(s_state.list);
    clear_status_locked();

    if (s_state.meetings.empty()) {
        set_status_locked("RACE SCHEDULE UNAVAILABLE");
        return;
    }

    const int center_index = find_center_meeting_index();
    for (size_t index = 0; index < s_state.meetings.size(); ++index) {
        const meeting_t &meeting = s_state.meetings[index];
        const bool highlighted = static_cast<int>(index) == center_index;
        lv_obj_t *row = create_row(s_state.list, RACE_ROW_HEIGHT, highlighted);

        if (highlighted) {
            lv_obj_t *marker = lv_obj_create(row);
            lv_obj_set_size(marker, 4, RACE_ROW_HEIGHT - 12);
            lv_obj_set_style_bg_color(marker, lv_color_hex(0xE10600), 0);
            lv_obj_set_style_bg_opa(marker, LV_OPA_COVER, 0);
            lv_obj_set_style_border_width(marker, 0, 0);
            lv_obj_set_style_radius(marker, 0, 0);
            lv_obj_align(marker, LV_ALIGN_LEFT_MID, 0, 0);
            lv_obj_set_scrollable(marker, false);
        }

        const lv_color_t name_color = meeting.cancelled ? lv_color_hex(0x676767) : lv_color_white();
        lv_obj_t *name = lv_label_create(row);
        lv_label_set_text(name, meeting.name.c_str());
        lv_obj_set_width(name, BSP_DISPLAY_PANEL_WIDTH - 68);
        lv_label_set_long_mode(name, LV_LABEL_LONG_DOT);
        lv_obj_align(name, LV_ALIGN_TOP_LEFT, 12, 7);
        style_label(name, name_color, &lv_font_montserrat_14);

        const std::string secondary = meeting.location + "  |  " + format_local_date(meeting.date_start);
        lv_obj_t *details = lv_label_create(row);
        lv_label_set_text(details, meeting.cancelled ? "CANCELLED" : secondary.c_str());
        lv_obj_align(details, LV_ALIGN_BOTTOM_LEFT, 12, -7);
        style_label(details, meeting.cancelled ? lv_color_hex(0x686868) : lv_color_hex(0xA0A0A0),
                    &lv_font_montserrat_14);
    }
    lv_obj_update_layout(s_state.list);
    if (center_index >= 0) {
        const int32_t target = center_index * RACE_ROW_HEIGHT -
                               (lv_obj_get_height(s_state.list) - RACE_ROW_HEIGHT) / 2;
        lv_obj_scroll_to_y(s_state.list, target, LV_ANIM_OFF);
    }
}

void render_sessions_locked()
{
    s_state.page = page_t::sessions;
    lv_obj_set_hidden(s_state.back, false);
    lv_label_set_text(s_state.title, "EVENT SESSIONS");
    lv_obj_set_pos(s_state.list, 12, SESSION_LIST_TOP);
    lv_obj_set_size(s_state.list, BSP_DISPLAY_PANEL_WIDTH - 24,
                    BSP_DISPLAY_PANEL_HEIGHT - SESSION_LIST_TOP - SESSION_LIST_BOTTOM_MARGIN);
    lv_obj_clean(s_state.list);
    clear_status_locked();

    if (s_state.selected_meeting < 0 ||
        static_cast<size_t>(s_state.selected_meeting) >= s_state.meetings.size()) {
        lv_label_set_text(s_state.subtitle, "");
        set_status_locked("SELECT A GRAND PRIX");
        return;
    }

    const meeting_t &meeting = s_state.meetings[s_state.selected_meeting];
    lv_label_set_text(s_state.subtitle, meeting.name.c_str());
    if (s_state.sessions.empty()) {
        set_status_locked("LOADING EVENT SESSIONS");
        return;
    }

    const time_t now = time(nullptr);
    for (size_t index = 0; index < s_state.sessions.size(); ++index) {
        const session_t &session = s_state.sessions[index];
        const bool disabled = session.cancelled || session.start_epoch > now;
        const bool selected = static_cast<int>(index) == s_state.selected_session;
        lv_obj_t *row = create_row(s_state.list, SESSION_ROW_HEIGHT, selected);
        const lv_color_t color = disabled ? lv_color_hex(0x666666) : lv_color_white();

        lv_obj_t *name = lv_label_create(row);
        lv_label_set_text(name, session.name.c_str());
        lv_obj_set_width(name, BSP_DISPLAY_PANEL_WIDTH / 2 - 30);
        lv_label_set_long_mode(name, LV_LABEL_LONG_DOT);
        lv_obj_align(name, LV_ALIGN_LEFT_MID, 12, 0);
        style_label(name, color, &lv_font_montserrat_14);

        std::string secondary = session.cancelled
                                    ? "CANCELLED"
                                    : format_local_date(session.date_start) + " LOCAL";
        lv_obj_t *details = lv_label_create(row);
        lv_label_set_text(details, secondary.c_str());
        lv_obj_set_width(details, BSP_DISPLAY_PANEL_WIDTH / 2 - 24);
        lv_label_set_long_mode(details, LV_LABEL_LONG_DOT);
        lv_obj_align(details, LV_ALIGN_RIGHT_MID, -10, 0);
        style_label(details, disabled ? lv_color_hex(0x626262) : lv_color_hex(0xA0A0A0),
                    &lv_font_montserrat_14);
    }
    lv_obj_update_layout(s_state.list);

    const int center_index = s_state.selected_session >= 0
                                 ? s_state.selected_session
                                 : static_cast<int>(std::distance(
                                       s_state.sessions.begin(),
                                       std::find_if(s_state.sessions.begin(), s_state.sessions.end(),
                                                    [now](const session_t &session) {
                                                        return !session.cancelled && session.start_epoch >= now;
                                                    })));
    if (center_index >= 0 && static_cast<size_t>(center_index) < s_state.sessions.size()) {
        const int32_t target = center_index * SESSION_ROW_HEIGHT -
                               (lv_obj_get_height(s_state.list) - SESSION_ROW_HEIGHT) / 2;
        lv_obj_scroll_to_y(s_state.list, target, LV_ANIM_OFF);
    }
}

void meetings_loader_task(void *argument)
{
    const int year = *static_cast<int *>(argument);
    delete static_cast<int *>(argument);

    std::vector<meeting_t> meetings;
    esp_err_t err = ESP_FAIL;
    char url[96] = {};
    std::snprintf(url, sizeof(url), "%s/meetings?year=%d", OPENF1_BASE_URL, year);
    for (int attempt = 0; attempt < 5 && err != ESP_OK; ++attempt) {
        std::string response;
        err = get_json(url, &response);
        if (err == ESP_OK) {
            err = parse_meetings(response, &meetings);
        }
        if (err != ESP_OK && attempt < 4) {
            vTaskDelay(pdMS_TO_TICKS(4000));
        }
    }

    if (lvgl_port_lock(1000)) {
        if (err == ESP_OK) {
            s_state.meetings = std::move(meetings);
            render_races_locked();
        } else {
            ESP_LOGE(TAG, "Unable to load race calendar: %s", esp_err_to_name(err));
            set_status_locked("SCHEDULE UNAVAILABLE");
        }
        lvgl_port_unlock();
    }
    vTaskDelete(nullptr);
}

void sessions_loader_task(void *argument)
{
    const uint32_t meeting_key = *static_cast<uint32_t *>(argument);
    delete static_cast<uint32_t *>(argument);

    char url[112] = {};
    std::snprintf(url, sizeof(url), "%s/sessions?meeting_key=%u", OPENF1_BASE_URL,
                  static_cast<unsigned>(meeting_key));
    std::string response;
    std::vector<session_t> sessions;
    esp_err_t err = get_json(url, &response);
    if (err == ESP_OK) {
        err = parse_sessions(response, meeting_key, &sessions);
    }

    if (lvgl_port_lock(1000)) {
        if (s_state.page == page_t::sessions && s_state.selected_meeting >= 0 &&
            s_state.meetings[s_state.selected_meeting].key == meeting_key) {
            if (err == ESP_OK) {
                s_state.sessions = std::move(sessions);
                render_sessions_locked();
            } else {
                ESP_LOGE(TAG, "Unable to load sessions: %s", esp_err_to_name(err));
                s_state.sessions.clear();
                set_status_locked("SESSIONS UNAVAILABLE");
            }
        }
        lvgl_port_unlock();
    }
    vTaskDelete(nullptr);
}

void request_sessions(uint32_t meeting_key)
{
    auto *argument = new (std::nothrow) uint32_t(meeting_key);
    if (argument == nullptr ||
        xTaskCreate(sessions_loader_task, "sessions_loader", 8192, argument, 3, nullptr) != pdPASS) {
        delete argument;
        if (lvgl_port_lock(1000)) {
            set_status_locked("SESSION REQUEST FAILED");
            lvgl_port_unlock();
        }
    }
}

void handle_click_locked(int x, int y, int *meeting_to_load)
{
    const int back_left = (BSP_DISPLAY_PANEL_WIDTH - BACK_TOUCH_TARGET_WIDTH) / 2;
    if (s_state.page == page_t::sessions && x >= back_left &&
        x < back_left + BACK_TOUCH_TARGET_WIDTH &&
        y >= BSP_DISPLAY_PANEL_HEIGHT - BACK_TOUCH_TARGET_HEIGHT &&
        y < BSP_DISPLAY_PANEL_HEIGHT) {
        s_state.sessions.clear();
        s_state.selected_meeting = -1;
        s_state.selected_session = -1;
        render_races_locked();
        return;
    }

    const int list_top = s_state.page == page_t::races ? 116 : SESSION_LIST_TOP;
    const int list_bottom = s_state.page == page_t::races
                                ? BSP_DISPLAY_PANEL_HEIGHT - 12
                                : BSP_DISPLAY_PANEL_HEIGHT - SESSION_LIST_BOTTOM_MARGIN;
    if (y < list_top || y >= list_bottom || x < 12 || x >= BSP_DISPLAY_PANEL_WIDTH - 12) {
        return;
    }

    const int row_height = s_state.page == page_t::races ? RACE_ROW_HEIGHT : SESSION_ROW_HEIGHT;
    const int content_y = y - list_top + lv_obj_get_scroll_y(s_state.list);
    const int index = content_y / row_height;

    if (s_state.page == page_t::races) {
        if (index < 0 || static_cast<size_t>(index) >= s_state.meetings.size() ||
            s_state.meetings[index].cancelled) {
            return;
        }
        s_state.selected_meeting = index;
        s_state.selected_session = -1;
        s_state.sessions.clear();
        render_sessions_locked();
        set_status_locked("LOADING EVENT SESSIONS");
        *meeting_to_load = static_cast<int>(s_state.meetings[index].key);
        return;
    }

    if (index < 0 || static_cast<size_t>(index) >= s_state.sessions.size()) {
        return;
    }
    const session_t &session = s_state.sessions[index];
    if (session.cancelled || session.start_epoch > time(nullptr)) {
        return;
    }
    s_state.selected_session = index;
    render_sessions_locked();
}

}  // namespace

esp_err_t session_selector_init(void)
{
    if (s_state.initialized) {
        return ESP_OK;
    }
    if (!lvgl_port_lock(1000)) {
        return ESP_FAIL;
    }

    s_state.year = get_current_year();
    s_state.screen = lv_obj_create(nullptr);
    lv_obj_set_style_bg_color(s_state.screen, lv_color_black(), 0);
    lv_obj_set_style_bg_opa(s_state.screen, LV_OPA_COVER, 0);
    lv_obj_set_style_border_width(s_state.screen, 0, 0);
    lv_obj_set_style_pad_all(s_state.screen, 0, 0);

    screen_header_create(s_state.screen);

    s_state.title = lv_label_create(s_state.screen);
    lv_label_set_text_fmt(s_state.title, "%d RACE CALENDAR", s_state.year);
    lv_obj_set_width(s_state.title, BSP_DISPLAY_PANEL_WIDTH - 24);
    lv_obj_align(s_state.title, LV_ALIGN_TOP_MID, 0, 70);
    lv_label_set_long_mode(s_state.title, LV_LABEL_LONG_DOT);
    style_label(s_state.title, lv_color_white(), &lv_font_montserrat_20);

    s_state.subtitle = lv_label_create(s_state.screen);
    lv_label_set_text(s_state.subtitle, "GRAND PRIX");
    lv_obj_set_width(s_state.subtitle, BSP_DISPLAY_PANEL_WIDTH - 24);
    lv_obj_align(s_state.subtitle, LV_ALIGN_TOP_MID, 0, 94);
    lv_label_set_long_mode(s_state.subtitle, LV_LABEL_LONG_DOT);
    style_label(s_state.subtitle, lv_color_hex(0xA0A0A0), &lv_font_montserrat_14);

    s_state.back = lv_label_create(s_state.screen);
    lv_label_set_text(s_state.back, "BACK");
    lv_obj_set_width(s_state.back, BACK_TOUCH_TARGET_WIDTH);
    lv_obj_set_style_text_align(s_state.back, LV_TEXT_ALIGN_CENTER, 0);
    lv_obj_align(s_state.back, LV_ALIGN_BOTTOM_MID, 0, -8);
    style_label(s_state.back, lv_color_hex(0xE10600), &lv_font_montserrat_14);
    lv_obj_set_hidden(s_state.back, true);

    s_state.list = lv_obj_create(s_state.screen);
    lv_obj_set_size(s_state.list, BSP_DISPLAY_PANEL_WIDTH - 24, BSP_DISPLAY_PANEL_HEIGHT - 128);
    lv_obj_set_pos(s_state.list, 12, 116);
    lv_obj_set_style_bg_opa(s_state.list, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(s_state.list, 0, 0);
    lv_obj_set_style_radius(s_state.list, 0, 0);
    lv_obj_set_style_pad_all(s_state.list, 0, 0);
    lv_obj_set_style_pad_row(s_state.list, 0, 0);
    lv_obj_set_layout(s_state.list, LV_LAYOUT_FLEX);
    lv_obj_set_flex_flow(s_state.list, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_scroll_dir(s_state.list, LV_DIR_VER);
    lv_obj_set_scrollbar_mode(s_state.list, LV_SCROLLBAR_MODE_OFF);

    s_state.status = lv_label_create(s_state.screen);
    lv_obj_set_width(s_state.status, BSP_DISPLAY_PANEL_WIDTH - 24);
    lv_obj_align(s_state.status, LV_ALIGN_CENTER, 0, 80);
    lv_obj_set_style_text_align(s_state.status, LV_TEXT_ALIGN_CENTER, 0);
    style_label(s_state.status, lv_color_hex(0xA0A0A0), &lv_font_montserrat_14);
    lv_label_set_text(s_state.status, "LOADING RACE CALENDAR");

    lv_scr_load(s_state.screen);
    s_state.initialized = true;
    lvgl_port_unlock();

    auto *year = new (std::nothrow) int(s_state.year);
    if (year == nullptr ||
        xTaskCreate(meetings_loader_task, "meetings_loader", 8192, year, 3, nullptr) != pdPASS) {
        delete year;
        if (lvgl_port_lock(1000)) {
            set_status_locked("SCHEDULE REQUEST FAILED");
            lvgl_port_unlock();
        }
        return ESP_ERR_NO_MEM;
    }
    return ESP_OK;
}

void session_selector_handle_touch(bool pressed, int x, int y)
{
    if (!s_state.initialized || !lvgl_port_lock(100)) {
        return;
    }

    int meeting_to_load = -1;
    if (pressed) {
        if (!s_state.pointer_down) {
            s_state.pointer_down = true;
            s_state.pointer_dragged = false;
            s_state.pointer_start_x = x;
            s_state.pointer_start_y = y;
            s_state.pointer_last_y = y;
            const int list_top = s_state.page == page_t::races ? 116 : SESSION_LIST_TOP;
            const int list_bottom = s_state.page == page_t::races
                                        ? BSP_DISPLAY_PANEL_HEIGHT - 12
                                        : BSP_DISPLAY_PANEL_HEIGHT - SESSION_LIST_BOTTOM_MARGIN;
            s_state.pointer_started_in_list = x >= 12 && x < BSP_DISPLAY_PANEL_WIDTH - 12 &&
                                              y >= list_top && y < list_bottom;
        } else if (s_state.pointer_started_in_list) {
            const int delta_y = y - s_state.pointer_last_y;
            if (std::abs(y - s_state.pointer_start_y) > 8 || std::abs(x - s_state.pointer_start_x) > 8) {
                s_state.pointer_dragged = true;
            }
            if (delta_y != 0) {
                lv_obj_scroll_by_bounded(s_state.list, 0, delta_y, LV_ANIM_OFF);
            }
            s_state.pointer_last_y = y;
        }
    } else if (s_state.pointer_down) {
        if (!s_state.pointer_dragged) {
            handle_click_locked(s_state.pointer_start_x, s_state.pointer_start_y, &meeting_to_load);
        }
        s_state.pointer_down = false;
        s_state.pointer_dragged = false;
        s_state.pointer_started_in_list = false;
    }
    lvgl_port_unlock();

    if (meeting_to_load >= 0) {
        request_sessions(static_cast<uint32_t>(meeting_to_load));
    }
}

bool session_selector_get_selected_session(uint32_t *meeting_key, uint32_t *session_key)
{
    if (meeting_key == nullptr || session_key == nullptr || !s_state.initialized ||
        !lvgl_port_lock(1000)) {
        return false;
    }

    const bool selected = s_state.selected_meeting >= 0 && s_state.selected_session >= 0 &&
                          static_cast<size_t>(s_state.selected_meeting) < s_state.meetings.size() &&
                          static_cast<size_t>(s_state.selected_session) < s_state.sessions.size();
    if (selected) {
        *meeting_key = s_state.meetings[s_state.selected_meeting].key;
        *session_key = s_state.sessions[s_state.selected_session].key;
    }
    lvgl_port_unlock();
    return selected;
}