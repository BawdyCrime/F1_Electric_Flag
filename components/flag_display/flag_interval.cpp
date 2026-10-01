#include "flag_interval.h"

#include "bsp_display.h"
#include "lvgl.h"

#include <algorithm>

extern "C" {
extern const uint8_t _binary_team_logo_rbr_rgb565_start[];
extern const uint8_t _binary_team_logo_mcl_rgb565_start[];
extern const uint8_t _binary_team_logo_fer_rgb565_start[];
extern const uint8_t _binary_team_logo_mer_rgb565_start[];
extern const uint8_t _binary_team_logo_ast_rgb565_start[];
extern const uint8_t _binary_team_logo_alp_rgb565_start[];
extern const uint8_t _binary_team_logo_wil_rgb565_start[];
extern const uint8_t _binary_team_logo_rb_rgb565_start[];
extern const uint8_t _binary_team_logo_aud_rgb565_start[];
extern const uint8_t _binary_team_logo_has_rgb565_start[];
extern const uint8_t _binary_team_logo_cad_rgb565_start[];
}

namespace {

constexpr int32_t PANEL_HEIGHT = BSP_DISPLAY_PANEL_WIDTH; // same square area used by the flag screens
constexpr int32_t ROW_HEIGHT = 36;
constexpr int32_t POSITION_COL_FLEX = 2;
constexpr int32_t TEAM_COL_FLEX = 6;
constexpr int32_t DRIVER_COL_FLEX = 6;
constexpr int32_t INTERVAL_COL_FLEX = 8;
constexpr int32_t TYRE_COL_FLEX = 2;
constexpr int32_t TEAM_LOGO_WIDTH = 56;
constexpr int32_t TEAM_LOGO_HEIGHT = 28;
constexpr int32_t DRIVER_BAR_WIDTH = 4;

constexpr lv_image_dsc_t make_team_logo(const uint8_t *data) {
    return {
        .header = {
            .magic = LV_IMAGE_HEADER_MAGIC,
            .cf = LV_COLOR_FORMAT_RGB565,
            .flags = 0,
            .w = TEAM_LOGO_WIDTH,
            .h = TEAM_LOGO_HEIGHT,
            .stride = static_cast<uint32_t>(TEAM_LOGO_WIDTH) * 2U,
            .reserved_2 = 0,
        },
        .data_size = static_cast<uint32_t>(TEAM_LOGO_WIDTH) * TEAM_LOGO_HEIGHT * 2U,
        .data = data,
        .reserved = nullptr,
        .reserved_2 = nullptr,
    };
}

static const lv_image_dsc_t TEAM_LOGO_RBR = make_team_logo(_binary_team_logo_rbr_rgb565_start);
static const lv_image_dsc_t TEAM_LOGO_MCL = make_team_logo(_binary_team_logo_mcl_rgb565_start);
static const lv_image_dsc_t TEAM_LOGO_FER = make_team_logo(_binary_team_logo_fer_rgb565_start);
static const lv_image_dsc_t TEAM_LOGO_MER = make_team_logo(_binary_team_logo_mer_rgb565_start);
static const lv_image_dsc_t TEAM_LOGO_AST = make_team_logo(_binary_team_logo_ast_rgb565_start);
static const lv_image_dsc_t TEAM_LOGO_ALP = make_team_logo(_binary_team_logo_alp_rgb565_start);
static const lv_image_dsc_t TEAM_LOGO_WIL = make_team_logo(_binary_team_logo_wil_rgb565_start);
static const lv_image_dsc_t TEAM_LOGO_RB = make_team_logo(_binary_team_logo_rb_rgb565_start);
static const lv_image_dsc_t TEAM_LOGO_AUD = make_team_logo(_binary_team_logo_aud_rgb565_start);
static const lv_image_dsc_t TEAM_LOGO_HAS = make_team_logo(_binary_team_logo_has_rgb565_start);
static const lv_image_dsc_t TEAM_LOGO_CAD = make_team_logo(_binary_team_logo_cad_rgb565_start);

lv_color_t team_color(flag_team_t team) {
    switch (team) {
        case FLAG_TEAM_RED_BULL_RACING:
            return lv_color_hex(0x3671C6);
        case FLAG_TEAM_MCLAREN:
            return lv_color_hex(0xF58020);
        case FLAG_TEAM_FERRARI:
            return lv_color_hex(0xE8002D);
        case FLAG_TEAM_MERCEDES:
            return lv_color_hex(0x27F4D2);
        case FLAG_TEAM_ASTON_MARTIN:
            return lv_color_hex(0x229971);
        case FLAG_TEAM_ALPINE:
            return lv_color_hex(0x00A1E8);
        case FLAG_TEAM_WILLIAMS:
            return lv_color_hex(0x64C4FF);
        case FLAG_TEAM_RACING_BULLS:
            return lv_color_hex(0x6692FF);
        case FLAG_TEAM_AUDI:
            return lv_color_hex(0xBB0A30);
        case FLAG_TEAM_HAAS:
            return lv_color_hex(0xB6BABD);
        case FLAG_TEAM_CADILLAC:
            return lv_color_hex(0xFFD700);
        default:
            return lv_color_white();
    }
}

const lv_image_dsc_t *team_logo_image(flag_team_t team) {
    switch (team) {
        case FLAG_TEAM_RED_BULL_RACING:
            return &TEAM_LOGO_RBR;
        case FLAG_TEAM_MCLAREN:
            return &TEAM_LOGO_MCL;
        case FLAG_TEAM_FERRARI:
            return &TEAM_LOGO_FER;
        case FLAG_TEAM_MERCEDES:
            return &TEAM_LOGO_MER;
        case FLAG_TEAM_ASTON_MARTIN:
            return &TEAM_LOGO_AST;
        case FLAG_TEAM_ALPINE:
            return &TEAM_LOGO_ALP;
        case FLAG_TEAM_WILLIAMS:
            return &TEAM_LOGO_WIL;
        case FLAG_TEAM_RACING_BULLS:
            return &TEAM_LOGO_RB;
        case FLAG_TEAM_AUDI:
            return &TEAM_LOGO_AUD;
        case FLAG_TEAM_HAAS:
            return &TEAM_LOGO_HAS;
        case FLAG_TEAM_CADILLAC:
            return &TEAM_LOGO_CAD;
        default:
            return nullptr;
    }
}

lv_color_t tyre_color(char tyre) {
    switch (tyre) {
        case 'S':
            return lv_color_hex(0xFF1E1E); // soft
        case 'M':
            return lv_color_hex(0xFFD500); // medium
        case 'H':
            return lv_color_white();       // hard
        case 'I':
            return lv_color_hex(0x00A550); // intermediate
        case 'W':
            return lv_color_hex(0x0057B8); // wet
        default:
            return lv_color_hex(0x808080);
    }
}

lv_obj_t *create_column(lv_obj_t *row, int32_t flex_grow) {
    lv_obj_t *col = lv_obj_create(row);
    lv_obj_set_size(col, lv_pct(100), lv_pct(100));
    lv_obj_set_flex_grow(col, flex_grow);
    lv_obj_set_style_bg_opa(col, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(col, 0, 0);
    lv_obj_set_style_pad_all(col, 2, 0);
    lv_obj_set_style_radius(col, 4, 0);
    lv_obj_set_scrollbar_mode(col, LV_SCROLLBAR_MODE_OFF);
    lv_obj_set_scrollable(col, false);
    return col;
}

lv_obj_t *create_label(lv_obj_t *parent, lv_color_t color) {
    lv_obj_t *label = lv_label_create(parent);
    lv_obj_set_style_text_color(label, color, 0);
    lv_obj_set_style_text_font(label, &lv_font_montserrat_20, 0);
    lv_obj_set_style_text_align(label, LV_TEXT_ALIGN_CENTER, 0);
    lv_obj_center(label);
    return label;
}

void create_row(lv_obj_t *panel, const flag_interval_row_t &row) {
    lv_obj_t *row_obj = lv_obj_create(panel);
    lv_obj_set_size(row_obj, lv_pct(100), ROW_HEIGHT);
    lv_obj_set_flex_flow(row_obj, LV_FLEX_FLOW_ROW);
    lv_obj_set_style_bg_opa(row_obj, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(row_obj, 0, 0);
    lv_obj_set_style_pad_all(row_obj, 0, 0);
    lv_obj_set_style_pad_gap(row_obj, 0, 0);
    lv_obj_set_scrollbar_mode(row_obj, LV_SCROLLBAR_MODE_OFF);
    lv_obj_set_scrollable(row_obj, false);

    lv_obj_t *position_col = create_column(row_obj, POSITION_COL_FLEX);
    lv_label_set_text_fmt(create_label(position_col, lv_color_white()), "%u", (unsigned)row.position);
    
    lv_obj_t *team_col = create_column(row_obj, TEAM_COL_FLEX);
    const lv_image_dsc_t *logo = team_logo_image(row.team);
    if (logo != nullptr) {
        lv_obj_t *team_image = lv_image_create(team_col);
        lv_image_set_src(team_image, logo);
        lv_obj_center(team_image);
    }

    lv_obj_t *driver_col = create_column(row_obj, DRIVER_COL_FLEX);
    lv_obj_t *driver_bar = lv_obj_create(driver_col);
    lv_obj_set_size(driver_bar, DRIVER_BAR_WIDTH, lv_pct(50));
    lv_obj_set_style_bg_color(driver_bar, team_color(row.team), 0);
    lv_obj_set_style_bg_opa(driver_bar, LV_OPA_COVER, 0);
    lv_obj_set_style_border_width(driver_bar, 0, 0);
    lv_obj_set_style_radius(driver_bar, 0, 0);
    lv_obj_align(driver_bar, LV_ALIGN_LEFT_MID, 4, 0);

    lv_obj_t *driver_label = create_label(driver_col, lv_color_white());
    lv_label_set_text(driver_label, row.driver_code);
    lv_obj_align(driver_label, LV_ALIGN_LEFT_MID, DRIVER_BAR_WIDTH + 10, 0);

    lv_obj_t *interval_col = create_column(row_obj, INTERVAL_COL_FLEX);
    lv_obj_t *interval_label = create_label(interval_col, lv_color_white());
    lv_label_set_text(interval_label, row.interval);
    lv_obj_align(interval_label, LV_ALIGN_RIGHT_MID, -4, 0);

    lv_obj_t *tyre_col = create_column(row_obj, TYRE_COL_FLEX);
    const char tyre_text[2] = {row.tyre, '\0'};
    lv_label_set_text(create_label(tyre_col, tyre_color(row.tyre)), tyre_text);
}

} // namespace

const size_t FLAG_INTERVAL_MAX_ROWS = PANEL_HEIGHT / ROW_HEIGHT;

void flag_interval_create(flag_view_t *view) {
    view->interval_panel = lv_obj_create(view->screen);
    lv_obj_set_size(view->interval_panel, BSP_DISPLAY_PANEL_WIDTH, PANEL_HEIGHT);
    lv_obj_align(view->interval_panel, LV_ALIGN_BOTTOM_MID, 0, 0);
    lv_obj_set_flex_flow(view->interval_panel, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_style_bg_color(view->interval_panel, lv_color_black(), 0);
    lv_obj_set_style_bg_opa(view->interval_panel, LV_OPA_COVER, 0);
    lv_obj_set_style_border_width(view->interval_panel, 0, 0);
    lv_obj_set_style_pad_all(view->interval_panel, 4, 0);
    lv_obj_set_style_pad_gap(view->interval_panel, 2, 0);
    lv_obj_set_scrollbar_mode(view->interval_panel, LV_SCROLLBAR_MODE_OFF);
    lv_obj_set_scrollable(view->interval_panel, false);
}

void flag_interval_update(flag_view_t *view, const flag_interval_row_t *rows, size_t row_count) {
    if (view->interval_panel == nullptr) {
        return;
    }
    lv_obj_clean(view->interval_panel);
    const size_t visible_rows = std::min(row_count, FLAG_INTERVAL_MAX_ROWS);
    for (size_t i = 0; i < visible_rows; ++i) {
        create_row(view->interval_panel, rows[i]);
    }
}
