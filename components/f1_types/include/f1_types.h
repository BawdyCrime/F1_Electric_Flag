#pragma once

#include <cstdint>
#include <ctime>
#include <string>

namespace f1 {

struct Meeting {
    uint32_t key = 0;
    std::string name;
    std::string location;
    bool cancelled = false;
    time_t start_epoch = 0;
    time_t end_epoch = 0;
};

struct Session {
    uint32_t key = 0;
    uint32_t meeting_key = 0;
    std::string name;
    bool cancelled = false;
    time_t start_epoch = 0;
};

}  // namespace f1
