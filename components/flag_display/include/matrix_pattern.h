#pragma once

#include <stdbool.h>
#include <stddef.h>

enum { MATRIX_PATTERN_SIZE = 32 };

typedef enum {
    MATRIX_PATTERN_SOLID,
    MATRIX_PATTERN_DOUBLE_YELLOW,
    MATRIX_PATTERN_SAFETY_CAR,
    MATRIX_PATTERN_VSC,
} matrix_pattern_type_t;

typedef struct {
    matrix_pattern_type_t type;
    char pixel;
    bool visible;
    bool triangle_state;
} matrix_pattern_t;

char matrix_pattern_get_pixel(size_t row, size_t column, void *user_data);