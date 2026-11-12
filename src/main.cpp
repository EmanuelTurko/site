#include "util/logging.hpp"
#include "http/common.hpp"
#include "stats.hpp"
#include "http/router.hpp"
#include "http/server.hpp"
#include "http/staticFiles.hpp"
#include "config.hpp"
#include "page.hpp"



int main(int argc,char* argv[]) {
    std::filesystem::path path;
    if (argc>1)
        path = argv[1];
    else path = std::filesystem::path(SITE_DEFAULT_ROOT) / "config.json";

    Config cfg{};
    tcp::endpoint endpoint{};
    std::optional<Page> page{};
    try {
        cfg = loadConfig(path);
        page.emplace(cfg.root,cfg.reload); // optional page is assigned here
        endpoint.address(asio::ip::make_address(cfg.address));
        endpoint.port(cfg.port); //basic tcp endpoint construction
    } catch (const std::exception &e) {
        logging::error(e.what());
        return 1;
    }

    Stats stats{};
    Router router{};
    std::filesystem::path root{cfg.root};
    router.getPrefix("/static/",staticFiles(root / "static","/static/"));


    router.get("/",
               [&page,&stats](const Request &, const RequestContext&) -> asio::awaitable<Response> {
                   json::object runtime{
                       {
                           "stats", {
                               {"uptime", Stats::formatUptime(stats.uptimeSeconds())},
                               {"requests", stats.requests() + 1}
                           }
                       },
                       {"playground_enabled", false},
                       {"redis_enabled", false}
                   };
                   try {
                     auto html = page->render(runtime);
                       co_return makeResponse(http::status::ok, html, "text/html; charset=utf-8");
                   } catch (const std::exception& e) {
                       logging::error(e.what());
                       auto body = std::format("template error: {}\n", e.what());
                       co_return makeResponse(http::status::internal_server_error,body,"text/plain");
                   }
               });
    router.get("/api/stats",
           [&stats](const Request &, const RequestContext&) -> asio::awaitable<Response> {
               json::object runtime{
                           {"uptime_s", stats.uptimeSeconds()},
                            {"requests", stats.requests()},
                            {"avg_us", stats.avgMicroseconds()}
               };
               co_return makeResponse(http::status::ok,json::serialize(runtime),"application/json");
           });
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
