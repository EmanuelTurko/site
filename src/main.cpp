#include "util/logging.hpp"
#include "http/common.hpp"
#include "stats.hpp"
#include "http/router.hpp"
#include "http/server.hpp"
#include "http/staticFiles.hpp"
constexpr int numThreads = 2;


int main() {
    Router router{};
    std::filesystem::path root{SITE_DEFAULT_ROOT};
    router.getPrefix("/static/",staticFiles(root / "static","/static/"));

    tcp::endpoint endpoint{};
    endpoint.address(asio::ip::make_address("0.0.0.0"));
    endpoint.port(18080); //basic tcp endpoint construction

    Stats stats{};
    asio::io_context ioc(numThreads);
    Server server(ioc,endpoint,router,stats);
    server.start();

    asio::signal_set signal(ioc,SIGINT,SIGTERM);
    signal.async_wait(
        [&ioc](const beast::error_code&,int) {
            ioc.stop();
        });
    logging::info("listening on http://localhost:18080 with 2 io thread(s)");

    std::vector<std::thread> threads;
    for (auto i=0; i < numThreads-1;++i) {
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
