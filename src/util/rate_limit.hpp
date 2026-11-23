#pragma once

#include <chrono>
#include <string>
#include <mutex>
#include <unordered_map>
class RateLimiter {
public:
    RateLimiter(double burst, double perSec):
    _burst(burst),
    _perSec(perSec){}


    auto allow(const std::string& key) -> bool { // key = client IP
        std::lock_guard<std::mutex> lg(_mu);
        auto now = clock::now();
        if (_buckets.size() > 10000) _buckets.clear();

        auto [it,inserted] = _buckets.try_emplace(key);
        if (inserted) {
            it->second = {.tokens = _burst, .last = now};
        }
        else {
            auto elapsed = std::chrono::duration<double>(now - it->second.last).count();
            it->second.tokens =  std::min(it->second.tokens + elapsed*_perSec,_burst);
            it->second.last = now;
        }
        auto& tokens = it->second.tokens;
        if (tokens >= 1) {
            tokens--;
            return true;
        }
        return false;
    }
private:
    using clock = std::chrono::steady_clock;
    struct Bucket {
        double tokens;
        clock::time_point last; // last = when we last refilled
    };

    double _burst;
    double _perSec;
    std::mutex _mu;
    std::unordered_map<std::string, Bucket> _buckets;

};