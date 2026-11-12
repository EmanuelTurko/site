#pragma once

#include "common.hpp"

#include <string>
#include <vector>


class Router {
public:
    auto get(std::string path, Handler h) -> void; // exact path, GET
    auto post(std::string path, Handler h) -> void; // exact path, POST
    auto getPrefix(std::string prefix, Handler h) -> void;  // any path starting with prefix, GET
    auto dispatch(const Request& req, const RequestContext& ctx) const -> asio::awaitable<Response>;
    private:
    struct Route {
        http::verb verb{};
        std::string path{};
        bool prefix{}; // true: match by starts-with, false : exact match
        Handler handler{};
    };

    auto add(http::verb verb, std::string path, bool prefix, Handler h) -> void;
    std::vector<Route> _routes{};
};