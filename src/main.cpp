#include "util/logging.hpp"
#include "http/common.hpp"
#include "stats.hpp"
#include "http/router.hpp"

auto session(tcp::socket socket, const Router& router) -> asio::awaitable<void> {
    try {
        beast::tcp_stream stream(std::move(socket));
        beast::flat_buffer buffer{};
        Request request{};

        co_await http::async_read(stream, buffer, request, asio::use_awaitable);

        auto resp = co_await router.dispatch(request,stream.socket().remote_endpoint().address().to_string());
        resp.prepare_payload();
        co_await http::async_write(stream, resp, asio::use_awaitable);

        stream.socket().shutdown(asio::socket_base::shutdown_send);
    } catch (const system_error &e) {
        logging::error(e.what());
    }
}
auto listen(tcp::endpoint endpoint, const Router& router) -> asio::awaitable<void> {
    try {
        auto executor = co_await asio::this_coro::executor; // executor holds the reference to the event loop
        tcp::acceptor acceptor(executor);
        acceptor.open(endpoint.protocol()); // get a socket from the OS, type protocol(ipv4/ipv6)
        acceptor.set_option(asio::socket_base::reuse_address(true));
        acceptor.bind(endpoint); // bind to specific addr
        acceptor.listen();

        while (true) {
            auto socket = co_await acceptor.async_accept(asio::use_awaitable); // wait for a client...
            asio::co_spawn(executor, session(std::move(socket),router), asio::detached);
            // start a new coroutine with socket ownership
        }
    } catch (const system_error& e) {
        logging::error(e.what());
    }
}

int main() {
    Router router{};

    router.get("/ping",
        [](const Request&, const std::string& ip) ->
        asio::awaitable<Response> {
            co_return makeResponse(http::status::ok, "pong\n", "text/plain");
        });

    const auto address = asio::ip::make_address("0.0.0.0");
    constexpr auto portNum = 18080;
    tcp::endpoint endpoint(address, portNum); //basic tcp endpoint construction

    asio::io_context ioc{};
    asio::co_spawn(ioc, listen(endpoint,router), asio::detached); // starts a coroutine
    ioc.run();
}
