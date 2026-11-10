#pragma once

#include "common.hpp"
#include "router.hpp"
#include "stats.hpp"

class Server {
public:
    Server(asio::io_context& ioc, tcp::endpoint endpoint, const Router& router, Stats& stats) :
    _ioc(ioc),
    _endpoint(std::move(endpoint)),
    _router(router),
    _stats(stats){}

    auto start() -> void; // returns immediately

    private:
    auto listen() -> asio::awaitable<void>;
    auto session(tcp::socket socket) -> asio::awaitable<void>;
    auto isNormalClose(const system_error &e) -> bool;

    asio::io_context& _ioc;
    tcp::endpoint _endpoint{};
    const Router& _router{};
    Stats& _stats;
};