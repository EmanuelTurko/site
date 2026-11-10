#include "util/logging.hpp"
#include "http/common.hpp"
#include "stats.hpp"
#include "http/router.hpp"
#include "http/server.hpp"
#include "http/staticFiles.hpp"
#include "config.hpp"

constexpr int numThreads = 2;


int main() {
    Config cfg{};
    try {
         cfg = loadConfig(std::filesystem::path(SITE_DEFAULT_ROOT) / "config.json");
    } catch (const std::exception& e) {
        logging::error(e.what());
        return 1;
    }

    Router router{};
    std::filesystem::path root{SITE_DEFAULT_ROOT};
    router.getPrefix("/static/",staticFiles(root / "static","/static/"));

    tcp::endpoint endpoint{};
    endpoint.address(asio::ip::make_address(cfg.address));
    endpoint.port(cfg.port); //basic tcp endpoint construction

    Stats stats{};
    asio::io_context ioc(numThreads);
    Server server(ioc,endpoint,router,stats);
    server.start();

    asio::signal_set signal(ioc,SIGINT,SIGTERM);
    signal.async_wait(
        [&ioc](const beast::error_code&,int) {
            ioc.stop();
        });
    logging::info(std::format("listening on http://localhost:{} with {} io threads",cfg.port,numThreads));

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
