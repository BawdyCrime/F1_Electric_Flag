#pragma once

#include "lvgl.h"

struct flag_view_t {
    lv_obj_t *screen = nullptr;
    lv_obj_t *square = nullptr;
    lv_obj_t *lap_header = nullptr;
    lv_obj_t *time_header = nullptr;
};