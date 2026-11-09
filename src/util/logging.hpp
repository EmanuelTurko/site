#pragma once

#include <string_view>
#include <chrono>
#include <mutex>
#include <iostream>


namespace logging {
    using std::chrono::system_clock;

    inline void write(std::string_view level, std::string_view message) {
        static std::mutex mu{};
        std::tm tm{}; // struct that contains a broken-down time information [tm_hour, tm_min...]
        auto now = system_clock::to_time_t(system_clock::now());
        localtime_r(&now, &tm);

        char stamp[16];
        std::strftime(stamp, sizeof(stamp), "%H:%M:%S", &tm); //string format time

        std::lock_guard lock(mu);
        std::cerr << stamp << ' ' << level << "  " << message << "\n";
    }

    inline void info(std::string_view message) {
        write("INFO ", message);
    }

    inline void warn(std::string_view message) {
        write("WARN ", message);
    }

    inline void error(std::string_view message) {
        write("ERROR", message);
    }
}
