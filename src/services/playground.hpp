#pragma once

#include <filesystem>

#include "http/common.hpp"
#include "util/rate_limit.hpp"

class TempDir {
public:
    TempDir();
    ~TempDir();
    TempDir(const TempDir&) = delete; // prevent copying
    TempDir& operator=(const TempDir&) = delete;
    auto ok() const -> bool; // // did the constructor manage to create it?
    auto path() const -> const std::filesystem::path&;

private:
    std::filesystem::path _path{};
};

class Playground {
    public:
    struct Config {
        std::string compiler; //empty = disabled
        std::size_t maxSource = 4096;
        std::size_t workers = 2;
    };

    explicit Playground(Config cfg);
    ~Playground();     // joins the pool

    auto enabled() const -> bool; // true if _cfg.compiler isn't empty
    auto coimpiler() const -> std::string&;
    auto handle(const Request& req,const RequestContext& ctx) -> asio::awaitable<Response>;

    private:
    auto compileAndRun(const std::string& code) -> json::object; //blocking

    Config _cfg{};
    asio::thread_pool _pool{};         // sized _cfg.workers
    std::atomic<std::size_t> _busy{0}; // how many compilers are running right now
    RateLimiter _limiter{15.0,0.5};
};

