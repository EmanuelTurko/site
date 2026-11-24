#include <gtest/gtest.h>

#include "services/playground.hpp"   // Playground, Playground::Config, Request, RequestContext, Response

#include <boost/asio.hpp>
#include <boost/beast/http.hpp>
#include <boost/json.hpp>

#include <cstdlib>
#include <filesystem>
#include <future>
#include <iostream>
#include <string>
#include <thread>

namespace asio = boost::asio;
namespace http = boost::beast::http;
namespace json = boost::json;

namespace {

std::filesystem::path hydroPath() {
    if (const char* env = std::getenv("HYDRO_COMPILER")) return env;
    const char* home = std::getenv("HOME");
    return std::filesystem::path(home ? home : "") / "compiler/cmake-build-debug-wsl/hydro";
}

// Runs pg.handle() to completion. The io_context runs on its own thread because
// handle() hops to the pool and then back to this executor.
Response call(Playground& pg, const std::string& body, const std::string& ip) {
    asio::io_context ioc;
    auto guard = asio::make_work_guard(ioc);
    std::thread t([&] { ioc.run(); });

    Request req{http::verb::post, "/api/run", 11};
    req.set(http::field::content_type, "application/json");
    req.body() = body;
    req.prepare_payload();

    RequestContext ctx{};
    ctx.remoteIp = ip;   // a different ip per test, so the rate limiter never trips

    auto fut = asio::co_spawn(ioc, pg.handle(req, ctx), asio::use_future);
    Response res = fut.get();
    guard.reset();
    t.join();
    return res;
}

std::string codeBody(const std::string& code) {
    return json::serialize(json::object{{"code", code}});
}

json::object bodyJson(const Response& res) {
    std::cout << res.result_int() << " " << res.body() << "\n";   // whole reply in the test log
    return json::parse(res.body()).as_object();
}

std::string str(const json::object& o, const char* key) {
    return json::value_to<std::string>(o.at(key));
}

Playground::Config cfg(std::string compiler, std::size_t maxSource = 16 * 1024) {
    Playground::Config c{};
    c.compiler = std::move(compiler);
    c.workers = 1;
    c.maxSource = maxSource;
    return c;
}

class PlaygroundEnabled : public ::testing::Test {
protected:
    void SetUp() override {
        if (!std::filesystem::exists(hydroPath()))
            GTEST_SKIP() << "hydro compiler not found at " << hydroPath() << " (set HYDRO_COMPILER)";
    }
};

}  // namespace

// ---------- 503 (no compiler needed) ----------

TEST(PlaygroundHandle, MissingCompilerIs503) {
    Playground pg(cfg("/definitely/not/hydro"));
    EXPECT_FALSE(pg.enabled());
    auto res = call(pg, codeBody("fun main() -> int { ret 5; }"), "10.0.0.1");
    EXPECT_EQ(res.result(), http::status::service_unavailable);
    EXPECT_FALSE(bodyJson(res).at("ok").as_bool());
    EXPECT_EQ(res[http::field::content_type], "application/json");
}

// ---------- 400 / 413 ----------

TEST_F(PlaygroundEnabled, NotJsonIs400) {
    Playground pg(cfg(hydroPath().string()));
    auto res = call(pg, "this is not json", "10.0.1.1");
    EXPECT_EQ(res.result(), http::status::bad_request);
    EXPECT_FALSE(bodyJson(res).at("ok").as_bool());
}

TEST_F(PlaygroundEnabled, JsonArrayIs400) {
    Playground pg(cfg(hydroPath().string()));
    EXPECT_EQ(call(pg, R"(["code"])", "10.0.1.2").result(), http::status::bad_request);
}

TEST_F(PlaygroundEnabled, MissingCodeIs400) {
    Playground pg(cfg(hydroPath().string()));
    auto res = call(pg, R"({"source":"fun main() -> int { ret 5; }"})", "10.0.1.3");
    EXPECT_EQ(res.result(), http::status::bad_request);
}

TEST_F(PlaygroundEnabled, CodeNotStringIs400) {
    Playground pg(cfg(hydroPath().string()));
    EXPECT_EQ(call(pg, R"({"code":42})", "10.0.1.4").result(), http::status::bad_request);
}

TEST_F(PlaygroundEnabled, SourceTooLargeIs413) {
    Playground pg(cfg(hydroPath().string(), 10));   // max 10 bytes of code
    auto res = call(pg, codeBody("fun main() -> int { ret 5; }"), "10.0.1.5");
    EXPECT_EQ(res.result(), http::status::payload_too_large);
    auto o = bodyJson(res);
    EXPECT_FALSE(o.at("ok").as_bool());
    EXPECT_NE(str(o, "error").find("max 10"), std::string::npos) << str(o, "error");
}

TEST_F(PlaygroundEnabled, SourceExactlyAtLimitIsAccepted) {
    std::string code = "fun main() -> int { ret 5; }";
    Playground pg(cfg(hydroPath().string(), code.size()));
    EXPECT_EQ(call(pg, codeBody(code), "10.0.1.6").result(), http::status::ok);
}

// ---------- full compile + run ----------

TEST_F(PlaygroundEnabled, ReturnValueBecomesExitCode) {
    Playground pg(cfg(hydroPath().string()));
    auto res = call(pg, codeBody("fun main() -> int { ret 5 * 20 / 50; }"), "10.0.2.1");
    ASSERT_EQ(res.result(), http::status::ok);
    auto r = bodyJson(res);
    EXPECT_TRUE(r.at("ok").as_bool());
    EXPECT_EQ(str(r, "stage"), "run");
    EXPECT_EQ(r.at("exit_code").to_number<int>(), 2);
    EXPECT_FALSE(str(r, "asm").empty());
    EXPECT_TRUE(r.contains("compile_ms"));
    EXPECT_TRUE(r.contains("run_ms"));
}

TEST_F(PlaygroundEnabled, InfiniteLoopHitsTimeLimit) {
    Playground pg(cfg(hydroPath().string()));
    auto res = call(pg, codeBody("fun main() -> int { def x = 0; while(1){ x = x + 1; } ret x; }"), "10.0.2.2");
    ASSERT_EQ(res.result(), http::status::ok);
    auto r = bodyJson(res);
    EXPECT_FALSE(r.at("ok").as_bool());
    EXPECT_EQ(str(r, "stage"), "run");
    EXPECT_NE(str(r, "error").find("time limit exceeded"), std::string::npos) << str(r, "error");
    EXPECT_FALSE(r.contains("exit_code"));
    EXPECT_LT(r.at("run_ms").to_number<long>(), 2500);
}

TEST_F(PlaygroundEnabled, MissingSemicolonIsACompileError) {
    Playground pg(cfg(hydroPath().string()));
    auto res = call(pg, codeBody("fun main() -> int { ret 5 }"), "10.0.2.3");
    ASSERT_EQ(res.result(), http::status::ok);
    auto r = bodyJson(res);
    EXPECT_FALSE(r.at("ok").as_bool());
    EXPECT_EQ(str(r, "stage"), "compile");
    EXPECT_NE(str(r, "error").find("1:27: error: expected ';'"), std::string::npos) << str(r, "error");
    EXPECT_EQ(str(r, "error").find("timed out"), std::string::npos);
}

TEST_F(PlaygroundEnabled, UnknownVariableIsACompileError) {
    Playground pg(cfg(hydroPath().string()));
    auto res = call(pg, codeBody("fun main() -> int { ret y; }"), "10.0.2.4");
    ASSERT_EQ(res.result(), http::status::ok);
    auto r = bodyJson(res);
    EXPECT_FALSE(r.at("ok").as_bool());
    EXPECT_EQ(str(r, "stage"), "compile");
    EXPECT_NE(str(r, "error").find("error"), std::string::npos) << str(r, "error");
}

TEST_F(PlaygroundEnabled, TempDirsAreCleanedUp) {
    auto dirs = [] {
        int n = 0;
        for (auto& e : std::filesystem::directory_iterator(std::filesystem::temp_directory_path()))
            n += e.is_directory() ? 1 : 0;
        return n;
    };
    Playground pg(cfg(hydroPath().string()));
    int before = dirs();
    call(pg, codeBody("fun main() -> int { ret 1; }"), "10.0.2.5");
    call(pg, codeBody("fun main() -> int { ret 1 }"), "10.0.2.6");   // compile-error path too
    EXPECT_EQ(dirs(), before) << "TempDir should remove its directory on every path";
}
