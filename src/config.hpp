#pragma once
#include <filesystem>

struct Config {
    std::filesystem::path root{}; // templates/, static/ content/ live here
    std::string address = "0.0.0.0";
    std::uint16_t port = 18080;
    unsigned threads = 2;
    bool reload{};

    struct {
        std::string compiler{}; // empty = playground off
        std::size_t maxSource = 4096;
        std::size_t workers = 2;
    } playground;

    struct {
        std::string host{}; // empty = redis demo off
        std::uint16_t port = 1234;
        std::string autostart; // redis server binary to launch; empty = don't
    }redis;
};

auto loadConfig(const std::filesystem::path& file)->Config;
