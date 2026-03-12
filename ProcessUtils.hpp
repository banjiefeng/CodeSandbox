#pragma once

#include <cstdint>
#include <functional>
#include <string>
#include <vector>

struct ProcessRunOptions {
    std::vector<std::string> command;
    std::string workingDirectory;
    std::string stdinPath;
    std::string stdoutPath;
    std::string stderrPath;
    int timeLimitMs = 0;
    std::int64_t memoryLimitBytes = 0;
    int processLimit = 0;
    std::function<bool()> beforeExec;
};

struct ProcessRunResult {
    int exitCode = -1;
    int signalNumber = 0;
    bool timedOut = false;
    int wallTimeMs = 0;
    std::int64_t peakMemoryBytes = 0;
    std::string errorMessage;
};

ProcessRunResult runProcessCommand(const ProcessRunOptions& options);
