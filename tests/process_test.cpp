#include <gtest/gtest.h>

#include "util/process.hpp"

#include <algorithm>
#include <cerrno>
#include <chrono>
#include <csignal>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <sstream>
#include <string>
#include <thread>
#include <vector>

#include <sys/wait.h>
#include <unistd.h>

namespace {

// "generous limits" from the guide: CPU 5 s, memory 512 MB, files 4 MB,
// fork allowed, no network isolation.
process::Limits generous(int timeoutMs = 5000) {
    return process::Limits{
        .cpuSeconds = 5,
        .memoryBytes = 512u << 20,
        .fileBytes = 4u << 20,
        .noFork = false,
        .isolateNetwork = false,
        .timeoutMs = timeoutMs,
    };
}

int exitCode(const process::Result& r) {
    return WIFEXITED(r.status) ? WEXITSTATUS(r.status) : -1;
}

std::vector<std::string> lines(const std::string& s) {
    std::vector<std::string> out;
    std::istringstream in(s);
    for (std::string line; std::getline(in, line);) out.push_back(line);
    return out;
}

// true if any process has exactly this command line (pgrep exits 0 on a match)
bool processExists(const std::string& cmdline) {
    std::string cmd = "pgrep -fx '" + cmdline + "' > /dev/null";
    int rc = std::system(cmd.c_str());
    return WIFEXITED(rc) && WEXITSTATUS(rc) == 0;
}

// After a test, this process must have no children left (none running, no zombies).
void expectNoChildrenLeft() {
    errno = 0;
    EXPECT_EQ(waitpid(-1, nullptr, WNOHANG), -1) << "a child is still around";
    EXPECT_EQ(errno, ECHILD);
}

}  // namespace

TEST(ProcessRun, EchoPrintsAndExitsZero) {
    auto r = process::run({"/bin/echo", "hi"}, "/tmp", generous());
    EXPECT_TRUE(r.started);
    EXPECT_FALSE(r.timedOut);
    EXPECT_EQ(r.output, "hi\n");
    ASSERT_TRUE(WIFEXITED(r.status));
    EXPECT_EQ(WEXITSTATUS(r.status), 0);
    expectNoChildrenLeft();
}

TEST(ProcessRun, SleepIsKilledAtTimeout) {
    auto r = process::run({"/bin/sleep", "5"}, "/tmp", generous(500));
    EXPECT_TRUE(r.timedOut);
    EXPECT_GE(r.ms, 450);
    EXPECT_LT(r.ms, 1500) << "should come back after ~0.5 s, not 5 s";
    ASSERT_TRUE(WIFSIGNALED(r.status));
    EXPECT_EQ(WTERMSIG(r.status), SIGKILL);
    expectNoChildrenLeft();
}

TEST(ProcessRun, MissingProgramExits127) {
    auto r = process::run({"/bin/nope"}, "/tmp", generous());
    EXPECT_TRUE(r.started);
    EXPECT_EQ(exitCode(r), 127);
    expectNoChildrenLeft();
}

TEST(ProcessRun, MissingCwdExits126) {
    auto r = process::run({"/bin/echo", "hi"}, "/no/such/dir", generous());
    EXPECT_EQ(exitCode(r), 126);
    EXPECT_EQ(r.output, "") << "echo must not have run";
    expectNoChildrenLeft();
}

TEST(ProcessRun, OutputIsCappedAt64KbAndPipeDoesNotJam) {
    auto r = process::run({"/bin/sh", "-c", "yes | head -c 200000"}, "/tmp", generous());
    EXPECT_FALSE(r.timedOut) << "the pipe jammed: drain stopped reading at the cap";
    EXPECT_EQ(r.output.size(), 65536u);
    EXPECT_EQ(exitCode(r), 0);
    expectNoChildrenLeft();
}

TEST(ProcessRun, NoLeakedFileDescriptors) {
    auto r = process::run({"/bin/ls", "/proc/self/fd"}, "/tmp", generous());
    ASSERT_EQ(exitCode(r), 0) << r.output;
    // 0 1 2, plus the one fd ls opens itself to read the directory
    auto fds = lines(r.output);
    EXPECT_LE(fds.size(), 4u) << "leaked fds:\n" << r.output;
    for (const auto& fd : {"0", "1", "2"})
        EXPECT_NE(std::find(fds.begin(), fds.end(), fd), fds.end()) << "missing fd " << fd;
    expectNoChildrenLeft();
}

TEST(ProcessRun, TimeoutKillsTheWholeGroup) {
    // 31 instead of 30 so we don't mistake an unrelated `sleep 30` for ours
    auto r = process::run({"/bin/sh", "-c", "sleep 31 & sleep 31"}, "/tmp", generous(300));
    EXPECT_TRUE(r.timedOut);
    std::this_thread::sleep_for(std::chrono::milliseconds(100));  // let SIGKILL land
    EXPECT_FALSE(processExists("sleep 31")) << "a grandchild survived: kill(pid) instead of kill(-pid)?";
    expectNoChildrenLeft();
}

TEST(ProcessSpawn, StartsInCwdAndReturnsImmediately) {
    auto dir = std::filesystem::temp_directory_path() / ("spawn_test_" + std::to_string(getpid()));
    std::filesystem::create_directories(dir);

    auto start = std::chrono::steady_clock::now();
    pid_t pid = process::spawn({"/bin/sh", "-c", "pwd > where.txt; sleep 1"}, dir);
    auto took = std::chrono::steady_clock::now() - start;

    ASSERT_GT(pid, 0);
    EXPECT_LT(took, std::chrono::milliseconds(500)) << "spawn must not wait for the program";

    int status = 0;
    ASSERT_EQ(waitpid(pid, &status, 0), pid);  // spawn doesn't reap; the caller does
    std::ifstream in(dir / "where.txt");
    std::string where;
    std::getline(in, where);
    EXPECT_EQ(std::filesystem::path(where), std::filesystem::canonical(dir));

    std::filesystem::remove_all(dir);
    expectNoChildrenLeft();
}

TEST(ProcessSpawn, MissingProgramFails) {
    // posix_spawn reports a failed exec as an error, so spawn returns -1
    pid_t pid = process::spawn({"/bin/nope"}, "/tmp");
    if (pid > 0) waitpid(pid, nullptr, 0);  // some libcs report it as exit 127 instead
    EXPECT_EQ(pid, -1);
    expectNoChildrenLeft();
}
