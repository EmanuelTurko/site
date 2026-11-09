#pragma once

#include <boost/asio.hpp>
#include <boost/beast.hpp>
#include <boost/json.hpp>

#include <string>
#include <string_view>
#include <functional>

// Names and helpers shared by every part of the HTTP layer.
namespace asio = boost::asio;
namespace beast = boost::beast;
namespace http = beast::http;
namespace json = boost::json;
using tcp = asio::ip::tcp;
using system_error = boost::system::system_error;

// The whole server uses one request and one response type: bodies are plain strings.
using Request = http::request<http::string_body>;
using Response = http::response<http::string_body>;

// A handler is a coroutine: it can co_await (network I/O, a hop to a thread
// pool, ...) and eventually produces a Response.
using Handler = std::function<asio::awaitable<Response>(const Request&, const std::string& ip)>;

inline auto makeResponse(http::status status, std::string body, std::string_view contentType)->Response {
    Response res{status,11};
    res.set(http::field::content_type, contentType);
    res.body() = std::move(body);
    return res;
}
inline auto jsonResponse(http::status status, const json::value& value)->Response {
    return makeResponse(status, json::serialize(value), "application/json");
}
inline auto jsonError(http::status status, std::string_view message)->Response {
    return jsonResponse(status, json::object{{"ok",false}, {"error",message}});
}