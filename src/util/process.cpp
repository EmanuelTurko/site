#include "process.hpp"

#include <fcntl.h>
#include <unistd.h>
#include <bits/fcntl-linux.h>
#include <signal.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <poll.h>

auto makeCharArgv(const std::vector<std::string>& argv) -> std::vector<char*> {
    std::vector<char*> v{};
    v.reserve(argv.size() +1);
    for (const auto& arg : argv) {
        v.emplace_back(const_cast<char*>(arg.data()));
    }
    v.emplace_back(nullptr);
    return v;
}
auto setLimiter(const int r,const rlim_t l) -> bool {
    rlimit lim(l,l);
    return setrlimit(r,&lim) == 0;
}

auto process::run(const std::vector<std::string> &argv, const std::filesystem::path &cwd,
                  const Limits &limits) -> Result {
    Result result{};

    auto cargv = makeCharArgv(argv);
    std::vector<std::string> envStrings = {
        "PATH=/usr/local/bin:/usr/bin:/bin",
        "LANG=C"
    };
    auto cenv = makeCharArgv(envStrings);
    auto scwd = cwd.string();

    int pipefd[2];
    if (pipe2(pipefd, O_CLOEXEC) == -1) return Result{};
    int devnull = open("/dev/null", O_RDONLY | O_CLOEXEC);
    if (devnull == -1) {
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
    if (pid == 0) {
        setpgid(0, 0);
        if (limits.isolateNetwork) unshare(CLONE_NEWUSER | CLONE_NEWNET);

        if (chdir(scwd.c_str()) != 0)
            _exit(126);
        dup2(devnull, 0);
        dup2(pipefd[1], 1);
        dup2(pipefd[1], 2);
        close_range(3, ~0U, 0);
        if (!setLimiter(RLIMIT_CPU, limits.cpuSeconds) ||
            !setLimiter(RLIMIT_AS, limits.memoryBytes) ||
            !setLimiter(RLIMIT_FSIZE, limits.fileBytes) ||
            !setLimiter(RLIMIT_CORE, 0) ||
            (limits.noFork && !setLimiter(RLIMIT_NPROC, 0)))
            _exit(126);

        execve(cargv[0], cargv.data(), cenv.data());
        _exit(127);
    }
    setpgid(pid, pid);
    close(devnull);
    close(pipefd[1]);

    result.started = true;
    constexpr auto maxOutput = 64 * 1024;

    auto flags = fcntl(pipefd[0],F_GETFL, 0);
    if (flags == -1) {
        perror("fcntl F_GETFL");
        kill(-pid,SIGKILL);
        waitpid(pid, nullptr, 0);
        close(pipefd[0]);
        return Result{};
    }
    if (fcntl(pipefd[0],F_SETFL, flags | O_NONBLOCK) == -1) {
        perror("fcntl F_SETFL");
        kill(-pid,SIGKILL);
        waitpid(pid, nullptr, 0);
        close(pipefd[0]);
        return Result{};
    }
    auto drain = [&] {
        char buf[4096];
        while (true) {
            auto n = read(pipefd[0], buf, sizeof(buf));

            if (n == 0) // EOF
                return;
            if (n == -1) // EAGAIN
                return;

            auto room = maxOutput - result.output.size();
            auto bytes = static_cast<size_t>(n);
            result.output.append(buf, std::min(room, bytes));
        }
    };

    pollfd fds[1];
    fds[0].fd = pipefd[0];
    fds[0].events = POLLIN;
    int status{};
    auto deadline = start + std::chrono::milliseconds(limits.timeoutMs);

    while (true) {
        constexpr auto tmout = 10;
        poll(fds, 1, tmout);
        drain();
        if (waitpid(pid, &status,WNOHANG) == pid)
            break;
        if (std::chrono::steady_clock::now() >= deadline) {
            kill(-pid,SIGKILL);
            waitpid(pid, &status, 0);
            result.timedOut = true;
            break;
        }
    }
    kill(-pid,SIGKILL);
    drain();
    close(pipefd[0]);

    auto now = std::chrono::steady_clock::now();
    result.ms = std::chrono::duration_cast<std::chrono::milliseconds>(now - start).count();
    result.status = status;

    return result;
}

auto process::spawn(const std::vector<std::string> &argv, const std::filesystem::path &cwd) -> pid_t {
    auto chars = makeCharArgv(argv);
}
