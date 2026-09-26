#include "serial_box_printer.h"

#include <algorithm>

#include "esp_log.h"

namespace app {

namespace {
// Box-drawing characters
const std::string TopLeft = "╭";
const std::string TopRight = "╮";
const std::string BottomLeft = "╰";
const std::string BottomRight = "╯";
const std::string Horizontal = "─";
const std::string Vertical = "│";
// ANSI escape codes
const std::string BoldPrefix = "\033[1m";
const std::string BoldSuffix = "\033[0m";

size_t utf8_display_width(const std::string& text) {
    size_t width = 0;
    for (size_t i = 0; i < text.size();) {
        const unsigned char ch = static_cast<unsigned char>(text[i]);

        if (ch == '\x1b') {
            size_t j = i + 1U;
            while (j < text.size() && text[j] != 'm') {
                ++j;
            }
            if (j < text.size()) {
                ++j;
            }
            i = j;
            continue;
        }

        if (ch < 0x80U) {
            ++width;
            ++i;
        } else if ((ch & 0xE0U) == 0xC0U) {
            ++width;
            i += 2U;
        } else if ((ch & 0xF0U) == 0xE0U) {
            ++width;
            i += 3U;
        } else if ((ch & 0xF8U) == 0xF0U) {
            ++width;
            i += 4U;
        } else {
            ++width;
            ++i;
        }
    }
    return width;
}

std::string repeated_line(const std::string& str, size_t width) {
    std::string line;
    line.reserve(width * str.size());
    for (size_t i = 0; i < width; ++i) {
        line += str;
    }
    return line;
}

std::string title_line(const std::string& title, size_t width) {
    const size_t visible_title_width = utf8_display_width(" " + title + " ");
    const size_t content_width = std::max(width, visible_title_width);
    const size_t right_fill = content_width > visible_title_width ? content_width - visible_title_width + 3U : 3U;

    std::string line;
    line.reserve(1U + 1U + BoldPrefix.size() + visible_title_width + right_fill + BoldSuffix.size() + 1U);
    line += TopLeft;
    line += Horizontal;
    line += BoldPrefix;
    line += " " + title + " ";
    line += BoldSuffix;
    line += repeated_line(Horizontal, right_fill);
    line += TopRight;
    return line;
}

std::string body_line(const std::string& line, size_t width) {
    std::string padded = line;
    const size_t visible_width = utf8_display_width(line);
    if (visible_width < width) {
        padded.append(width - visible_width, ' ');
    }
    std::string result;
    result.reserve(4U + padded.size());
    result += Vertical;
    result += "  ";
    result += padded;
    result += "  ";
    result += Vertical;
    return result;
}

std::string close_line(size_t width) {
    std::string line;
    line.reserve(width + 6U);
    line += BottomLeft;
    line += repeated_line(Horizontal, width + 4U);
    line += BottomRight;
    return line;
}

}  // namespace

std::string pad_field(const std::string& value, std::size_t width) {
    const size_t display_width = utf8_display_width(value);
    if (display_width >= width) {
        return value;
    }
    return value + std::string(width - display_width, ' ');
}

SerialBoxPrinter::SerialBoxPrinter(const std::string& title)
    : title_(title), width_(std::max(utf8_display_width(title), 48U)) {}

size_t SerialBoxPrinter::width() const {
    return width_;
}

void SerialBoxPrinter::add_body_line(const std::string& line) {
    rows_.push_back(line);
    width_ = std::max(width_, utf8_display_width(line));
}

void SerialBoxPrinter::add_body_bullet(const std::string& text, size_t indent_level) {
    std::string bullet_line;
    const std::string bullet = "•";
    bullet_line.reserve(indent_level * 2U + bullet.size() + 1U + text.size());
    bullet_line.append(indent_level, ' ');
    bullet_line += bullet;
    bullet_line.push_back(' ');
    bullet_line.append(text);

    rows_.push_back(bullet_line);
    width_ = std::max(width_, utf8_display_width(bullet_line));
}

void SerialBoxPrinter::add_blank_body() {
    rows_.push_back("");
}

void SerialBoxPrinter::print_title() {
    printf("%s\n", title_line(title_, width()).c_str());
}

void SerialBoxPrinter::print_body() {
    for (const auto& row : rows_) {
        printf("%s\n", body_line(row, width()).c_str());
    }
}

void SerialBoxPrinter::print_close() {
    printf("%s\n", close_line(width()).c_str());
}

void SerialBoxPrinter::print() {
    print_title();
    print_body();
    print_close();
}

void SerialBoxPrinter::print(const std::string& title, const std::vector<std::string>& rows) {
    SerialBoxPrinter printer(title);
    for (const auto& row : rows) {
        printer.add_body_line(row);
    }
    printer.print();
}

}  // namespace app
