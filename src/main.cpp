#include "util/logging.hpp"
#include "http/common.hpp"
#include "stats.hpp"

auto session(tcp::socket socket) -> asio::awaitable<void> {
    try {
        beast::tcp_stream stream(std::move(socket));
        beast::flat_buffer buffer{};
        Request request{};

        co_await http::async_read(stream, buffer, request, asio::use_awaitable);

        auto resp = makeResponse(http::status::ok, "hello from C++\n", "text/plain");
        resp.prepare_payload();
        co_await http::async_write(stream, resp, asio::use_awaitable);

        stream.socket().shutdown(asio::socket_base::shutdown_send);
    } catch (const system_error &e) {
        logging::error(e.what());
    }
}
auto listen(tcp::endpoint endpoint) -> asio::awaitable<void> {
    try {
        auto executor = co_await asio::this_coro::executor; // executor holds the reference to the event loop
        tcp::acceptor acceptor(executor);
        acceptor.open(endpoint.protocol()); // get a socket from the OS, type protocol(ipv4/ipv6)
        acceptor.set_option(asio::socket_base::reuse_address(true));
        acceptor.bind(endpoint); // bind to specific addr
        acceptor.listen();

        while (true) {
            auto socket = co_await acceptor.async_accept(asio::use_awaitable); // wait for a client...
            asio::co_spawn(executor, session(std::move(socket)), asio::detached);
            // start a new coroutine with socket ownership
        }
    } catch (const system_error& e) {
        logging::error(e.what());
    }
}

int main() {
    const auto address = asio::ip::make_address("0.0.0.0");
    constexpr auto portNum = 18080;
    tcp::endpoint endpoint(address, portNum); //basic tcp endpoint construction

    asio::io_context ioc{};
    asio::co_spawn(ioc, listen(endpoint), asio::detached); // starts a coroutine
    ioc.run();
}
