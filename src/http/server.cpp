#include "server.hpp"
#include "util/logging.hpp"

auto Server::start() -> void {

    _acceptor.open(_endpoint.protocol()); // get a socket from the OS, type protocol(ipv4/ipv6)
    _acceptor.set_option(asio::socket_base::reuse_address(true));
    _acceptor.bind(_endpoint);  // bind to specific addr
    _acceptor.listen();


    asio::co_spawn(_ioc, listen(), [](const std::exception_ptr &e) {
        try {
            if (e) std::rethrow_exception(e);
        } catch (const std::exception &ex) {
            logging::error(ex.what());
        }
    });
}

auto Server::listen() -> asio::awaitable<void> {
    try {
        auto executor = co_await asio::this_coro::executor;
        while (true) {
            auto socket = co_await _acceptor.async_accept(asio::use_awaitable); // wait for a client...
            asio::co_spawn(executor, session(std::move(socket)), asio::detached);
            // start a new coroutine with socket ownership
        }
    } catch (const system_error &e) {
        _ioc.stop();
        logging::error(e.what());
    }
}

auto Server::session(tcp::socket socket) -> asio::awaitable<void> {
    beast::tcp_stream stream(std::move(socket));
    beast::flat_buffer buffer{};
    boost::system::error_code ec{};

    constexpr std::uint64_t limit = 64 * 1024;
    auto ip = stream.socket().remote_endpoint(ec).address().to_string();

    try {
        while (true) {
            stream.expires_after(std::chrono::seconds(30));

            auto parser = http::request_parser<http::string_body>();
            parser.body_limit(limit);

            co_await http::async_read(stream, buffer, parser, asio::use_awaitable);
            auto req = parser.release();

            auto start = std::chrono::steady_clock::now();
            Response res{};
            try {
                res = co_await _router.dispatch(req, RequestContext{ip});
            } catch (const std::exception &e) {
                logging::error(e.what());
                res = makeResponse(http::status::internal_server_error, "internal server error", "text/plain");
            }
            _stats.record(
                duration_cast<std::chrono::microseconds>(
                    std::chrono::steady_clock::now() - start)
            );

            res.keep_alive(req.keep_alive());
            res.version(req.version());
            res.set(http::field::server, "emanuel-site (C++20, Boost.Beast)");
            res.prepare_payload();

            co_await http::async_write(stream, res, asio::use_awaitable);
            if (!res.keep_alive()) break;
        }
    } catch (const system_error &e) {
        if (!isNormalClose(e))
            logging::warn(ip + " " + e.what());
    }
    stream.socket().shutdown(asio::socket_base::shutdown_send, ec);
}

auto Server::isNormalClose(const system_error &e) -> bool {
    return e.code() == http::error::end_of_stream ||
           e.code() == beast::error::timeout ||
           e.code() == asio::error::connection_reset ||
           e.code() == asio::error::eof ||
           e.code() == asio::error::operation_aborted;
}
