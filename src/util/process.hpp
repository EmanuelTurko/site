#pragma once

#include <sys/resource.h>
#include <string>
#include <vector>
#include <filesystem>

namespace process {
    struct Limits {
        rlim_t cpuSeconds{};
        rlim_t memoryBytes{};
        rlim_t fileBytes{};
        bool noFork{};          // the program may not start other programs
        bool isolateNetwork{};  // the program gets no network
        int timeoutMs{};        // wall-clock time before we kill it
    };

    struct Result {
        bool started{};
        bool timedOut{};
        int status{};           // from waitpid (decode with WIFEXITED etc.)
        std::string output{};   // stdout + stderr, at most 64 KB
        long ms{};              // how long it ran
    };

    auto run(const std::vector<std::string>& argv, const std::filesystem::path& cwd, const Limits& limits) -> Result;
    auto spawn(const std::vector<std::string>& argv,const std::filesystem::path& cwd) -> pid_t;
}
