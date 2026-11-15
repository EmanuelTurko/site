#include "process.hpp"

#include <fcntl.h>
#include <unistd.h>
#include <bits/fcntl-linux.h>

auto makeCharArgv(const std::vector<std::string>& argv) -> std::vector<char*> {
    std::vector<char*> v{};
    v.reserve(argv.size() +1);
    for (const auto& arg : argv) {
        v.emplace_back(const_cast<char*>(arg.data()));
    }
    v.emplace_back(nullptr);
    return v;
}
auto process::run(const std::vector<std::string> &argv, const std::filesystem::path &cwd, const Limits &limits) -> Result {

    auto chars = makeCharArgv(argv);
    std::vector<std::string> envStrings ={
        "PATH=/usr/local/bin:/usr/bin:/bin",
        "LANG=C"
    };
    auto envChars = makeCharArgv(envStrings);
    auto cwdString = cwd.string();

    int pipefd[2];
    if (pipe2(pipefd, O_CLOEXEC) == 1) return Result{};
    int devnull = open("/dev/null", O_RDONLY | O_CLOEXEC);
    if  (devnull == -1) {
        close(pipefd[0]);
        close(pipefd[1]);
        return Result{};
    }
    auto start = std::chrono::steady_clock::now();
    pid_t pid = fork();

    if (pid < 0) {
        close(pipefd[0]);
        close(pipefd[1]);
        close(devnull);
        return Result{.started = false};
    }
}

auto process::spawn(const std::vector<std::string> &argv, const std::filesystem::path &cwd) -> pid_t {
    auto chars = makeCharArgv(argv);
}
