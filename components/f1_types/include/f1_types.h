#pragma once

#include <cstdint>
#include <ctime>
#include <string>
#include <vector>

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
    time_t end_epoch = 0;
};

struct Driver {
    uint32_t number = 0;
    std::string code;  // 3-letter acronym
    std::string team_name;
    std::string team_colour;  // hex without '#'
};

struct Position {
    uint32_t driver_number = 0;
    uint32_t position = 0;
};

// Gap fields are pre-formatted ("+0.842", "+1 LAP", "") since the API mixes numbers and strings.
struct Interval {
    uint32_t driver_number = 0;
    std::string interval;
    std::string gap_to_leader;
};

struct Stint {
    uint32_t driver_number = 0;
    std::string compound;  // SOFT, MEDIUM, HARD, INTERMEDIATE, WET
    uint32_t lap_start = 0;
    uint32_t lap_end = 0;  // 0 when unknown/in progress
};

struct LapTime {
    uint32_t driver_number = 0;
    uint32_t lap_number = 0;
    time_t start_epoch = 0;
    double duration_s = 0;  // completed laps only
};

// One running period of a session, e.g. Q1 of qualifying.
struct Segment {
    time_t start_epoch = 0;
    time_t end_epoch = 0;  // 0 when no finish message was seen
};

// Race control flag / safety car message.
struct RaceControl {
    time_t when = 0;
    std::string category;  // "Flag", "SafetyCar" or "SessionStatus"
    std::string flag;      // GREEN, YELLOW, DOUBLE YELLOW, RED, CLEAR, CHEQUERED...
    std::string scope;     // Track, Sector, Driver
    std::string message;
    uint32_t sector = 0;
};

// Timing state of a session at one instant, used for replay.
struct TimingSnapshot {
    std::vector<Position> positions;  // latest per driver, sorted by position
    std::vector<Interval> intervals;  // latest per driver
    uint32_t lap = 0;
};

}  // namespace f1
