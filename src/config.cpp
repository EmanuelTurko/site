#include "config.hpp"

#include <fstream>

#include "http/common.hpp"

namespace {
    template<class T>
    auto readField(const json::object& obj, std::string_view key, T& out) -> void {
        const auto it = obj.find(key);
        if ( it == obj.cend()) return;

        auto r = json::try_value_to<T>(it->value());
        if (!r)
            throw std::runtime_error(std::format("config: \"{}\" has the wrong type",key));
        out = *r;
    }
    auto nestedObj(const json::object& obj, std::string_view key) -> const json::object* {
        const auto it = obj.find(key);
        if (it == obj.cend()) return nullptr;
        return it->value().if_object();
    }
    auto resolvePath(const std::filesystem::path& base,const std::string& p) -> std::string {
        if (p.empty()) return "";
        if (std::filesystem::path(p).is_absolute()) return p;
        return (base/p).string();
    }
};


auto loadConfig(const std::filesystem::path &file)->Config {

    std::ifstream f(file,std::ios::binary);
    if (!f) {
        throw std::runtime_error("file err");
    }
    std::string body(std::istreambuf_iterator<char>(f),{});
    boost::system::error_code ec;
    auto v = json::parse(std::move(body),ec);
    if (ec || !v.is_object())
        throw std::runtime_error("json parsin err");

    const auto& obj = *v.if_object();
    const auto& base = file.parent_path();
    Config cfg{};
    cfg.root = base;

    if (auto objPtr = nestedObj(obj,"playground"))
        readField(*objPtr,"compiler",cfg.playground.compiler);
    cfg.playground.compiler = resolvePath( base,cfg.playground.compiler);

    if (auto objPtr = nestedObj(obj,"redis"))
        readField(*objPtr,"autostart",cfg.redis.autostart);
    cfg.redis.autostart = resolvePath(base,cfg.redis.autostart);

    std::string r = cfg.root;
    readField(obj,"root",r);
    cfg.root = r;

    readField(obj,"address",cfg.address);
    readField(obj,"port",cfg.port);
    readField(obj,"threads",cfg.threads);
    readField(obj,"reload",cfg.reload);

    cfg.root = resolvePath(base,cfg.root);
    if (cfg.threads == 0) cfg.threads = 1;
    return cfg;
}