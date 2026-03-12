#include "ProcessUtils.hpp"

#include <fcntl.h>
#include <signal.h>
#include <sys/resource.h>
#include <sys/stat.h>
#include <sys/time.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <unistd.h>

#include <filesystem>
#include <fstream>
#include <cerrno>
#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstring>
#include <cstdlib>
#include <thread>
#include <vector>

namespace {

constexpr int kSandboxChdirFailed = 241;
constexpr int kSandboxStdinRedirectFailed = 242;
constexpr int kSandboxStdoutRedirectFailed = 243;
constexpr int kSandboxStderrRedirectFailed = 244;
constexpr int kSandboxMemoryLimitFailed = 245;
constexpr int kSandboxBeforeExecFailed = 246;
constexpr int kSandboxExecFailed = 247;

bool redirectToFile(const std::string& path, int targetFd, int flags, mode_t mode = 0644)
{
    if (path.empty())
    {
        return true;
    }

    int fd = open(path.c_str(), flags, mode);
    if (fd < 0)
    {
        return false;
    }

    if (dup2(fd, targetFd) < 0)
    {
        close(fd);
        return false;
    }

    close(fd);
    return true;
}

void cleanupCgroup(pid_t pid)
{
    const char* cgroupRoot = std::getenv("XDOJ_CGROUP_ROOT");
    if (cgroupRoot == nullptr || *cgroupRoot == '\0')
    {
        return;
    }

    namespace fs = std::filesystem;
    const fs::path cgPath = fs::path(cgroupRoot) / ("judge-" + std::to_string(pid));
    std::error_code ec;
    if (!fs::exists(cgPath, ec))
    {
        return;
    }

    // 尝试杀掉遗留进程（cgroup v2 支持 cgroup.kill）
    std::ofstream killFile((cgPath / "cgroup.kill").string().c_str(), std::ios::out | std::ios::binary);
    if (killFile.is_open())
    {
        killFile << "1";
    }

    ec.clear();
    fs::remove_all(cgPath, ec);
}

}

ProcessRunResult runProcessCommand(const ProcessRunOptions& options)
{
    ProcessRunResult result;
    if (options.command.empty())
    {
        result.errorMessage = "命令为空。";
        return result;
    }

    const std::chrono::steady_clock::time_point start = std::chrono::steady_clock::now();
    pid_t pid = fork();
    if (pid < 0)
    {
        result.errorMessage = std::string("fork 失败: ") + std::strerror(errno);
        return result;
    }

    if (pid == 0)
    {
        if (!options.workingDirectory.empty() && chdir(options.workingDirectory.c_str()) != 0)
        {
            _exit(kSandboxChdirFailed);
        }

        if (!redirectToFile(options.stdinPath, STDIN_FILENO, O_RDONLY))
        {
            _exit(kSandboxStdinRedirectFailed);
        }
        if (!redirectToFile(options.stdoutPath, STDOUT_FILENO, O_CREAT | O_WRONLY | O_TRUNC))
        {
            _exit(kSandboxStdoutRedirectFailed);
        }
        if (!redirectToFile(options.stderrPath, STDERR_FILENO, O_CREAT | O_WRONLY | O_TRUNC))
        {
            _exit(kSandboxStderrRedirectFailed);
        }

        if (options.memoryLimitBytes > 0)
        {
            struct rlimit memoryLimit;
            memoryLimit.rlim_cur = static_cast<rlim_t>(options.memoryLimitBytes);
            memoryLimit.rlim_max = static_cast<rlim_t>(options.memoryLimitBytes);
            if (setrlimit(RLIMIT_AS, &memoryLimit) != 0)
            {
                _exit(kSandboxMemoryLimitFailed);
            }
        }

        if (options.processLimit > 0)
        {
            struct rlimit processLimit;
            processLimit.rlim_cur = static_cast<rlim_t>(options.processLimit);
            processLimit.rlim_max = static_cast<rlim_t>(options.processLimit);
            if (setrlimit(RLIMIT_NPROC, &processLimit) != 0)
            {
                _exit(125);
            }
        }

        if (options.timeLimitMs > 0)
        {
            struct rlimit cpuLimit;
            const rlim_t seconds = static_cast<rlim_t>(std::max(1.0, std::ceil(options.timeLimitMs / 1000.0)));
            cpuLimit.rlim_cur = seconds;
            cpuLimit.rlim_max = seconds + 1;
            if (setrlimit(RLIMIT_CPU, &cpuLimit) != 0)
            {
                _exit(126);
            }
        }

        if (options.beforeExec && !options.beforeExec())
        {
            _exit(kSandboxBeforeExecFailed);
        }

        std::vector<char*> argv;
        argv.reserve(options.command.size() + 1);
        for (const std::string& arg : options.command)
        {
            argv.push_back(const_cast<char*>(arg.c_str()));
        }
        argv.push_back(nullptr);

        execvp(argv[0], argv.data());
        _exit(kSandboxExecFailed);
    }

    int status = 0;
    struct rusage usage;
    std::memset(&usage, 0, sizeof(usage));

    for (;;)
    {
        pid_t waitResult = wait4(pid, &status, WNOHANG, &usage);
        if (waitResult == pid)
        {
            break;
        }
        if (waitResult < 0)
        {
            result.errorMessage = std::string("wait4 失败: ") + std::strerror(errno);
            return result;
        }

        const auto now = std::chrono::steady_clock::now();
        result.wallTimeMs = static_cast<int>(
            std::chrono::duration_cast<std::chrono::milliseconds>(now - start).count());
        if (options.timeLimitMs > 0 && result.wallTimeMs > options.timeLimitMs)
        {
            result.timedOut = true;
            kill(pid, SIGKILL);
            wait4(pid, &status, 0, &usage);
            break;
        }

        std::this_thread::sleep_for(std::chrono::milliseconds(2));
    }

    const auto end = std::chrono::steady_clock::now();
    result.wallTimeMs = static_cast<int>(
        std::chrono::duration_cast<std::chrono::milliseconds>(end - start).count());
    result.peakMemoryBytes = static_cast<std::int64_t>(usage.ru_maxrss) * 1024;

    if (WIFEXITED(status))
    {
        result.exitCode = WEXITSTATUS(status);
    }
    else if (WIFSIGNALED(status))
    {
        result.signalNumber = WTERMSIG(status);
    }

    cleanupCgroup(pid);
    return result;
}
