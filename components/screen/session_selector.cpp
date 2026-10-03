#include "session_selector.h"

#include "bsp_display.h"
#include "screen_header.h"
#include "esp_lvgl_port.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "lvgl.h"

#include <algorithm>
#include <cstdio>
#include <cstdlib>
#include <ctime>
#include <string>
#include <vector>

namespace {

constexpr int32_t RACE_ROW_HEIGHT = 62;
constexpr int32_t SESSION_ROW_HEIGHT = 58;
constexpr int32_t SESSION_LIST_TOP = 136;
constexpr int32_t SESSION_LIST_BOTTOM_MARGIN = 52;
constexpr int32_t BACK_TOUCH_TARGET_WIDTH = 120;
constexpr int32_t BACK_TOUCH_TARGET_HEIGHT = 48;
static const char *TAG = "session_selector";

using meeting_t = f1::Meeting;
using session_t = f1::Session;

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
    session_selector_callbacks_t callbacks = {};
    bool initialized = false;
    bool pointer_down = false;
    bool pointer_dragged = false;
    int pointer_start_x = 0;
    int pointer_start_y = 0;
    int pointer_last_y = 0;
    bool pointer_started_in_list = false;
};

static selector_state_t s_state;

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

std::string format_local_date(time_t utc)
{
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

        const std::string secondary = meeting.location + "  |  " + format_local_date(meeting.start_epoch);
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
                                    : format_local_date(session.start_epoch);
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

struct click_result_t {
    bool back = false;
    uint32_t meeting_key = 0;
    uint32_t session_key = 0;
};

void handle_click_locked(int x, int y, click_result_t *result)
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
        result->back = true;
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
        result->meeting_key = s_state.meetings[index].key;
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
    result->meeting_key = session.meeting_key;
    result->session_key = session.key;
}

}  // namespace

esp_err_t session_selector_init(int year, const session_selector_callbacks_t &callbacks)
{
    if (s_state.initialized) {
        return ESP_OK;
    }
    if (!lvgl_port_lock(1000)) {
        return ESP_FAIL;
    }

    s_state.year = year;
    s_state.callbacks = callbacks;
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

    return ESP_OK;
}

void session_selector_handle_touch(bool pressed, int x, int y)
{
    if (!s_state.initialized || !lvgl_port_lock(100)) {
        return;
    }

    click_result_t click;
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
            handle_click_locked(s_state.pointer_start_x, s_state.pointer_start_y, &click);
        }
        s_state.pointer_down = false;
        s_state.pointer_dragged = false;
        s_state.pointer_started_in_list = false;
    }
    lvgl_port_unlock();

    if (click.back) {
        if (s_state.callbacks.on_back != nullptr) {
            s_state.callbacks.on_back(s_state.callbacks.context);
        }
    } else if (click.session_key != 0) {
        if (s_state.callbacks.on_session_selected != nullptr) {
            s_state.callbacks.on_session_selected(click.meeting_key, click.session_key, s_state.callbacks.context);
        }
    } else if (click.meeting_key != 0 && s_state.callbacks.on_meeting_selected != nullptr) {
        s_state.callbacks.on_meeting_selected(click.meeting_key, s_state.callbacks.context);
    }
}

void session_selector_show_meetings(const std::vector<f1::Meeting> &meetings)
{
    if (!s_state.initialized || !lvgl_port_lock(1000)) {
        return;
    }
    s_state.meetings = meetings;
    s_state.sessions.clear();
    s_state.selected_meeting = -1;
    render_races_locked();
    lvgl_port_unlock();
}

void session_selector_show_sessions(uint32_t meeting_key, const std::vector<f1::Session> &sessions)
{
    if (!s_state.initialized || !lvgl_port_lock(1000)) {
        return;
    }
    if (s_state.page == page_t::sessions && s_state.selected_meeting >= 0 &&
        s_state.meetings[s_state.selected_meeting].key == meeting_key) {
        s_state.sessions = sessions;
        render_sessions_locked();
    }
    lvgl_port_unlock();
}

void session_selector_show_status(const char *text)
{
    if (!s_state.initialized || !lvgl_port_lock(1000)) {
        return;
    }
    set_status_locked(text);
    lvgl_port_unlock();
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