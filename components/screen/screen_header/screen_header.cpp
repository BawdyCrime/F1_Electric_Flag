#include "screen_header.h"

extern "C" const uint8_t _binary_f1_logo_192x48_rgb565_start[];

static const lv_image_dsc_t F1_LOGO_IMAGE = {
    .header = {
        .magic = LV_IMAGE_HEADER_MAGIC,
        .cf = LV_COLOR_FORMAT_RGB565,
        .flags = 0,
        .w = 192,
        .h = 48,
        .stride = 384,
        .reserved_2 = 0,
    },
    .data_size = 192 * 48 * 2,
    .data = _binary_f1_logo_192x48_rgb565_start,
    .reserved = nullptr,
    .reserved_2 = nullptr,
};

void screen_header_create(lv_obj_t *parent)
{
    lv_obj_t *logo = lv_image_create(parent);
    lv_image_set_src(logo, &F1_LOGO_IMAGE);
    lv_obj_align(logo, LV_ALIGN_TOP_MID, 0, 8);
}