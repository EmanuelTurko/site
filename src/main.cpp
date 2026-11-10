#include "util/logging.hpp"
#include "http/common.hpp"
#include "stats.hpp"
#include "http/router.hpp"
#include "http/server.hpp"

int main() {
    constexpr int threadNum = 2;
    Router router{};
    tcp::endpoint endpoint{};
    endpoint.address(asio::ip::make_address("0.0.0.0"));
    endpoint.port(18080); //basic tcp endpoint construction

    Stats stats{};
    asio::io_context ioc(threadNum);
    Server server(ioc,endpoint,router,stats);
    server.start();



    asio::signal_set signal(ioc,SIGINT,SIGTERM);
    signal.async_wait(
        [&ioc](const beast::error_code&,int) {
            ioc.stop();
        });
    logging::info("listening on http://localhost:18080 with 2 io thread(s)");

    std::vector<std::thread> threads;
    for (auto i=0; i < threadNum-1;++i) {
        threads.emplace_back([&ioc]() {
            ioc.run();
        });
    }
    ioc.run();
    for (auto& t : threads) {
        t.join();
    }
    logging::info("bye");

}
