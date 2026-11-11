#pragma once

#include <filesystem>
#include <string>
#include <boost/json.hpp>
#include <memory>

#include "site/mustache.hpp"

class Page {
public:
    Page(std::filesystem::path root,bool reload) :
    _root(std::move(root)),
    _reload(reload) {
        _current = load();
    }
    auto render(const boost::json::object& runtime) -> std::string;

private:

    struct Snapshot {
        mustache::TemplateSet templates;
        boost::json::value content; // parsed content/site.json
    };

    auto load() const -> std::shared_ptr<const Snapshot>; // read everything from disk
    auto snapshot() -> std::shared_ptr<const Snapshot>;   // the one to use for this request

    std::filesystem::path _root;
    bool _reload;
    std::mutex _mu;
    std::shared_ptr<const Snapshot> _current;
};
