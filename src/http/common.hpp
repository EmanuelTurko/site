#pragma once

#include <boost/asio.hpp>
#include <boost/beast.hpp>
#include <boost/json.hpp>

namespace asio = boost::asio;
namespace beast = boost::beast;
namespace http = beast::http;
namespace json = boost::json;
using tcp = asio::ip::tcp;



// The whole server uses one request and one response type: bodies are plain strings.
using Request = http::request<http::string_body>;
using Response = http::response<http::string_body>;


using Handler = std::function<asio::awaitable<Response>(const Request&, const std::string& ip)>;

inline Response makeResponse(http::status status, std::string body, std::string_view contentType) {
    Response res{status,11};
    res.set(http::field::content_type, contentType);
    res.body() = std::move(body);
    return res;
}

inline Response jsonResponse(http::status status, const json::value& value) {
    return makeResponse(status, json::serialize(value), "application/json");
}

inline Response jsonError(http::status status, std::string_view message) {
    return jsonResponse(status, json::object{{"ok",false}, {"error",message}});
}