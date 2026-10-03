#include "app_time.h"

#include <cstdio>
#include <cstdlib>
#include <ctime>

int app_current_year()
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
