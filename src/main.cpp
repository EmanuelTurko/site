#include "util/logging.hpp"
#include "http/common.hpp"
#include "stats.hpp"
#include "http/router.hpp"
#include "http/server.hpp"
#include "http/staticFiles.hpp"
#include "config.hpp"



int main(int argc,char* argv[]) {

    std::filesystem::path path;
    if (argc>1)
        path = argv[1];
    else path = std::filesystem::path(SITE_DEFAULT_ROOT) / "config.json";

    Config cfg{};
    tcp::endpoint endpoint{};
    try {
        cfg = loadConfig(path);
        endpoint.address(asio::ip::make_address(cfg.address));
        endpoint.port(cfg.port); //basic tcp endpoint construction
    } catch (const std::exception &e) {
        logging::error(e.what());
        return 1;
    }

    Router router{};
    std::filesystem::path root{cfg.root};
    router.getPrefix("/static/",staticFiles(root / "static","/static/"));

    Stats stats{};
    asio::io_context ioc(cfg.threads);
    Server server(ioc,endpoint,router,stats);

    try {
        server.start();
    } catch (const std::exception& e) {
        logging::error(e.what());
        return 1;
    }

    asio::signal_set signal(ioc,SIGINT,SIGTERM);
    signal.async_wait(
        [&ioc](const beast::error_code&,int) {
            ioc.stop();
        });
    logging::info(std::format("listening on http://localhost:{} with {} io threads",cfg.port,cfg.threads));

    std::vector<std::thread> threads;
    for (unsigned i=0; i < cfg.threads-1;++i) {
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
