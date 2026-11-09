#pragma once

#include <chrono>
#include <atomic>

class Stats {
public:
    using clock = std::chrono::steady_clock;

    auto record(std::chrono::microseconds handlerTime)-> void { // one request finished
        _requests.fetch_add(1,std::memory_order_relaxed);
        _totalMicroseconds+= handlerTime.count();
    }
    auto requests() const -> std::uint64_t {
        return _requests.load(std::memory_order_relaxed);
    }
    auto avgMicroseconds() const -> std::uint64_t {
        auto req = _requests.load(std::memory_order_relaxed);
        auto total = _totalMicroseconds.load(std::memory_order_relaxed);
        return req > 0? total / req : 0;
    }
    auto uptimeSeconds() const -> std::uint64_t {
        auto duration = clock::now() - _started;
        return std::chrono::duration_cast<std::chrono::seconds>(duration).count();
    }
    static auto formatUptime(std::uint64_t seconds) -> std::string {
        std::chrono::seconds s{seconds};
        auto d = duration_cast<std::chrono::days>(s);
        s -= d;

        auto h = duration_cast<std::chrono::hours>(s);
        s -= h;

        auto m = duration_cast<std::chrono::minutes>(s);
        s -= m;

        if (d.count() > 0) {
            return std::format("{}d {}h",d.count(),h.count());
        }
        if (h.count() > 0) {
            return std::format("{}h {}m",h.count(),m.count());
        }
        if (m.count() > 0) {
            return std::format("{}m {}s",m.count(),s.count());
        }
        return std::format("{}s",s.count());
    }

private:
    clock::time_point _started = clock::now();
    std::atomic<std::uint64_t> _requests{};
    std::atomic<std::uint64_t> _totalMicroseconds{};

};
