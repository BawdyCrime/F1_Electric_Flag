#include "serial_box_printer.h"

#include <algorithm>

#include "esp_log.h"

namespace app {
namespace {

size_t calc_width(const std::string& title, const std::vector<std::string>& rows) {
    size_t max_width = title.size();
    for (const auto& row : rows) {
        max_width = std::max(max_width, row.size());
    }
    return std::max<size_t>(max_width, 48U);
}

std::string repeated_horizontal_line(size_t width) {
    static const char kHorizontalUtf8[] = "\xE2\x94\x80";
    std::string line;
    line.reserve(width * 3);
    for (size_t i = 0; i < width; ++i) {
        line += kHorizontalUtf8;
    }
    return line;
}

void append_repeated_utf8_horizontal(std::string& line, size_t count) {
    static const char kHorizontalUtf8[] = "\xE2\x94\x80";
    for (size_t i = 0; i < count; ++i) {
        line += kHorizontalUtf8;
    }
}

std::string repeated_horizontal_line_with_title(const std::string& title, size_t width) {
    const std::string title_block = " " + title + " ";
    const size_t content_width = std::max(width, title_block.size());
    const size_t right_fill = content_width > title_block.size() ? content_width - title_block.size() - 1 : 0U;

    std::string line;
    line.reserve(2U + title_block.size() + right_fill * 3U);
    line += "╭";
    line += "─";
    line += title_block;
    append_repeated_utf8_horizontal(line, right_fill);
    line += "╮";
    return line;
}

std::string repeated_horizontal_line_close(size_t width) {
    std::string line;
    line.reserve(width + 2U);
    line += "╰";
    line += repeated_horizontal_line(width);
    line += "╯";
    return line;
}

}  // namespace

SerialBoxPrinter::SerialBoxPrinter(const char* tag, const std::string& title)
    : tag_(tag), title_(title), width_(calc_width(title, {})) {}

size_t SerialBoxPrinter::width() const {
    return width_;
}

std::string SerialBoxPrinter::body_line_string_(const std::string& line) const {
    std::string padded = line;
    if (padded.size() < width()) {
        padded.append(width() - padded.size(), ' ');
    }
    return padded;
}

void SerialBoxPrinter::add_body_line(const std::string& line) {
    rows_.push_back(line);
    width_ = std::max(width_, calc_width(title_, rows_));
}

void SerialBoxPrinter::add_blank_body() {
    rows_.push_back("");
}

void SerialBoxPrinter::print_title() {
    const std::string top_line = repeated_horizontal_line_with_title(title_, width()+4);
    ESP_LOGI(tag_, "%s", top_line.c_str());
}

void SerialBoxPrinter::print_body_line(const std::string& line) {
    const std::string body = body_line_string_(line);
    ESP_LOGI(tag_, "│  %-*s  │", static_cast<int>(width()), body.c_str());
}

void SerialBoxPrinter::print_blank_body() {
    print_body_line("");
}

void SerialBoxPrinter::print_body() {
    for (const auto& row : rows_) {
        print_body_line(row);
    }
}

void SerialBoxPrinter::print_close() {
    ESP_LOGI(tag_, "%s", repeated_horizontal_line_close(width()+4).c_str());
}

void SerialBoxPrinter::print() {
    print_title();
    print_body();
    print_close();
}

void SerialBoxPrinter::print(const char* tag, const std::string& title, const std::vector<std::string>& rows) {
    SerialBoxPrinter printer(tag, title);
    for (const auto& row : rows) {
        printer.add_body_line(row);
    }
    printer.print();
}

}  // namespace app
