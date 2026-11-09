#pragma once

#include <string_view>
#include <chrono>
#include <mutex>
#include <iostream>


namespace logging {
    using std::chrono::system_clock;
    inline auto write(std::string_view level, std::string_view message)->void {
        static std::mutex mu{};
        std::tm tm{}; // struct that contains a broken-down time information [tm_hour, tm_min...]
        auto now = system_clock::to_time_t(system_clock::now());
        localtime_r(&now, &tm);

        char stamp[16];
        std::strftime(stamp, sizeof(stamp), "%H:%M:%S", &tm); //string format time

        std::lock_guard lock(mu);
        std::cerr << stamp << ' ' << level << "  " << message << "\n";
    }
    inline auto info(std::string_view message)->void {
        write("INFO ", message);
    }
    inline auto warn(std::string_view message)->void {
        write("WARN ", message);
    }
    inline auto error(std::string_view message)->void {
        write("ERROR", message);
    }
}
