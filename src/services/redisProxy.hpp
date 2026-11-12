#pragma once

#include <string>

#include "../http/common.hpp"
#include "util/rateLimit.hpp"

class RedisProxy {
public:
    struct Config {
        std::string host; // empty = disabled
        std::uint16_t port = 1234;
    };
    struct Segment { // one labelled piece of the bytes, for the slide
        std::string label; // e.g len 7
        std::string bytes; // the raw bytes of that piece
    };

    explicit RedisProxy(Config cfg);
    auto enabled() const -> bool; // true if _cfg.host isn't empty
    auto config() const -> const Config&;
    auto handle(const Request& req, const RequestContext& ctx) -> asio::awaitable<Response>;

private:
    static auto roundTrip(beast::tcp_stream& stream, const std::vector<std::string>& args, std::vector<Segment>* sent)
    -> asio::awaitable<std::optional<std::string>>;

    Config _cfg;
    RateLimiter _limiter{30.0,2.0};
};
