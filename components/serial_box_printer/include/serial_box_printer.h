#pragma once

#include <string>
#include <vector>

namespace app {

class SerialBoxPrinter {
public:
    SerialBoxPrinter(const char* tag, const std::string& title);

    void add_body_line(const std::string& line);
    void add_blank_body();

    void print_title();
    void print_body_line(const std::string& line);
    void print_blank_body();
    void print_body();
    void print_close();
    void print();

    static void print(const char* tag, const std::string& title, const std::vector<std::string>& rows);

private:
    size_t width() const;
    std::string body_line_string_(const std::string& line) const;

    const char* tag_;
    std::string title_;
    std::vector<std::string> rows_;
    size_t width_;
};

}  // namespace app
