#pragma once

#include <filesystem>
#include <random>

#include "http/common.hpp"
#include "util/logging.hpp"
#include "util/rate_limit.hpp"

class TempDir {
public:
    TempDir() {
        std::error_code ec;
        auto path = std::filesystem::temp_directory_path(ec).string() + "/e2-XXXXXX";
        if (ec) return;
        if (auto temp = mkdtemp(path.data())) _path = temp;
    }

    ~TempDir() {
        std::error_code ec;
        if (ok()) std::filesystem::remove_all(_path, ec);
    }

    auto ok() const -> bool { // did the constructor manage to create it?
        return !_path.empty();
    }
    auto path() const -> const std::filesystem::path & {
        return _path;
    }

    TempDir(const TempDir &) = delete; // prevent copying
    TempDir &operator=(const TempDir &) = delete;

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
    ~Playground();    // joins the pool

    auto enabled() const -> bool; // true if _cfg.compiler isn't empty
    auto compiler() const -> std::string&;
    auto handle(const Request& req,const RequestContext& ctx) -> asio::awaitable<Response>;

    private:
        auto compileAndRun(const std::string& code) -> json::object; //blocking


    Config _cfg{};
    asio::thread_pool _pool{};         // sized _cfg.workers
    std::atomic<std::size_t> _busy{0}; // how many compilers are running right now
    RateLimiter _limiter{15.0,0.5};
};

