#include "router.hpp"

auto Router::get(std::string path, Handler h) -> void {
    add(http::verb::get, std::move(path), false, std::move(h));
}

auto Router::post(std::string path, Handler h) -> void {
    add(http::verb::post, std::move(path), false, std::move(h));
}

auto Router::getPrefix(std::string prefix, Handler h) -> void {
    add(http::verb::get, std::move(prefix), true, std::move(h));
}

auto Router::dispatch(const Request &req, const std::string &ip) const -> asio::awaitable<Response> {
    std::string_view sv = req.target();
    sv = sv.substr(0, sv.find('?'));
    bool methodNotAllowed{};

    for (const auto &[verb, path, prefix, handler]: _routes) {
        if (prefix ? sv.starts_with(path) : sv == path) {
            if (verb == req.method()) co_return co_await handler(req, ip);
            methodNotAllowed = true;
        }
    }
    
    co_return methodNotAllowed
                  ? jsonError(http::status::method_not_allowed, "method not allowed")
                  : makeResponse(http::status::not_found, "404 not found\n","text/plain");
}

auto Router::add(http::verb verb, std::string path, bool prefix, Handler h) -> void {
    _routes.emplace_back(Route{
        .verb = verb,
        .path = std::move(path),
        .prefix = prefix,
        .handler = std::move(h)
    });
}
