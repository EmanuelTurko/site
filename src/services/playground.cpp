#include "playground.hpp"

#include <fstream>

#include "util/fileio.hpp"
#include "util/process.hpp"
#include "util/unit.hpp"

namespace {
    auto readFile(const std::filesystem::path& p,const std::size_t cap) -> std::string {
        std::string o(cap,'\0');
        auto stream = std::ifstream(p,std::ios::binary);
        stream.read(o.data(),cap);
        o.resize(stream.gcount());
        return o;
    }
    auto extractErrorLines(std::string& output) {
        std::string o;
        auto stream = std::istringstream(output);
        std::string line;
        while (std::getline(stream,line)){
            if (line.find("error") != std::string::npos)
                o += line + '\n';
        }
        return o.empty()? output : o;
    }
}
Playground::Playground(Config cfg) : _cfg(std::move(cfg)),
                                     _pool(_cfg.workers) {
    std::error_code ec{};
    if (!_cfg.compiler.empty()) {
        auto path = std::filesystem::canonical(_cfg.compiler, ec);
        if (ec) {
            logging::warn(ec.message() + _cfg.compiler);
            _cfg.compiler.clear();
            return;
        }
        if (access(path.c_str(),X_OK) != 0) {
            logging::warn(strerror(errno) + path.string());
            _cfg.compiler.clear();
            return;
        }
        _cfg.compiler = path;
    }
}

Playground::~Playground() {
    _pool.join();
}

auto Playground::compileAndRun(const std::string &code) -> json::object {
    json::object obj{};
    using namespace std::chrono_literals;
    using enum unit::size;

    TempDir dir{};
    if (!dir.ok())
        return json::object{
            {"ok", false},
            {"error", "could not create work dir"}
        };
    {
        auto stream = std::ofstream(dir.path()/"main.et",std::ios::binary);
        stream.write(code.c_str(),code.length());
    }
    constexpr int maxOutput = 32*1024;
    auto res = process::run({_cfg.compiler, "main.et"}, dir.path(),
                 {
                     .cpuSeconds = 5,
                     .memoryBytes = unit::bytes(512,MB),
                     .fileBytes = unit::bytes(4,MB),
                     .noFork = false,
                     .isolateNetwork = false,
                     .timeoutMs = std::chrono::milliseconds{5s}.count()
                 });
    obj["compile_ms"] = res.ms;
    obj["asm"] = readFile(dir.path()/"out.asm",maxOutput);

    if (!res.started || res.timedOut || res.status != 0 || !std::filesystem::exists(dir.path() / "out")) {
        obj["ok"] = false;
        obj["stage"] = "compile";
        obj["error"] = res.timedOut? "compiler timed out" : extractErrorLines(res.output);
        return obj;
    }

    res = process::run({dir.path() / "out"}, dir.path(),
                             {
                                 .cpuSeconds = 1,
                                 .memoryBytes = unit::bytes(64,MB),
                                 .fileBytes = unit::bytes(1,MB),
                                 .noFork = true,
                                 .isolateNetwork = true,
                                 .timeoutMs = std::chrono::milliseconds{2s}.count()
                             });
    obj["ok"] = true;
    obj["stage"] = "run";
    obj["run_ms"] = res.ms;
    obj["output"] = res.output;


    if (WIFSIGNALED(res.status)) {
        if (auto sig = WTERMSIG(res.status); res.timedOut || sig == SIGKILL || sig == SIGXCPU) {
            obj["ok"] = false;
            obj["error"] = "time limit exceeded (1 s CPU / 2 s wall), infinite loop?";
        } else {
            obj["ok"] = false;
            obj["error"] = std::string("program terminated by signal:") + strsignal(sig);
        }
    }
    if (WIFEXITED(res.status)) {
        obj["exit_code"] = WEXITSTATUS(res.status);
    }
    return obj;
}
