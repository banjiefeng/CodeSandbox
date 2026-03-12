#include "SandboxRuntime.hpp"

#include "CgroupManager.hpp"
#include "FilesystemManager.hpp"
#include "NamespaceManager.hpp"
#include "SeccompManager.hpp"
#include "../languageRunners/LanguageRunner.hpp"
#include "../ProcessUtils.hpp"

#include <algorithm>
#include <cctype>
#include <csignal>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <sstream>
#include <unistd.h>

namespace {

constexpr int kSandboxInternalExitMin = 241;
constexpr int kSandboxInternalExitMax = 247;

std::string readTextFile(const std::string& path)
{
    std::ifstream input(path.c_str(), std::ios::in | std::ios::binary);
    if (!input.is_open())
    {
        return "";
    }

    std::ostringstream buffer;
    buffer << input.rdbuf();
    return buffer.str();
}

std::string trimRight(const std::string& text)
{
    std::size_t end = text.size();
    while (end > 0)
    {
        const char ch = text[end - 1];
        if (ch != '\n' && ch != '\r' && ch != ' ' && ch != '\t')
        {
            break;
        }
        --end;
    }
    return text.substr(0, end);
}

std::string toLower(std::string text)
{
    for (char& ch : text)
    {
        ch = static_cast<char>(std::tolower(static_cast<unsigned char>(ch)));
    }
    return text;
}

bool looksLikeMemoryExceeded(const ProcessRunResult& processResult,
                             std::int64_t memoryLimit,
                             const std::string& stderrText)
{
    if (memoryLimit > 0 && processResult.peakMemoryBytes > memoryLimit)
    {
        return true;
    }

    const std::string lowered = toLower(stderrText);
    return lowered.find("bad_alloc") != std::string::npos ||
           lowered.find("memoryerror") != std::string::npos ||
           lowered.find("outofmemoryerror") != std::string::npos ||
           lowered.find("cannot allocate memory") != std::string::npos ||
           lowered.find("memory limit") != std::string::npos;
}

JudgeStatus detectCaseStatus(const ProcessRunResult& processResult,
                             std::int64_t memoryLimit,
                             const std::string& stderrText)
{
    if (processResult.exitCode >= kSandboxInternalExitMin &&
        processResult.exitCode <= kSandboxInternalExitMax)
    {
        return JudgeStatus::SE;
    }
    if (processResult.timedOut)
    {
        return JudgeStatus::TLE;
    }
    if (processResult.signalNumber == SIGKILL || processResult.signalNumber == SIGXCPU)
    {
        return JudgeStatus::TLE;
    }
    if (looksLikeMemoryExceeded(processResult, memoryLimit, stderrText))
    {
        return JudgeStatus::MLE;
    }
    if (processResult.signalNumber != 0 || processResult.exitCode != 0)
    {
        return JudgeStatus::RE;
    }
    return JudgeStatus::AC;
}

bool shouldAttachGlobalMessage(JudgeStatus status)
{
    return status == JudgeStatus::RE || status == JudgeStatus::TLE || status == JudgeStatus::MLE || status == JudgeStatus::SE;
}

bool writeSandboxError(const std::string& message)
{
    if (message.empty())
    {
        return false;
    }
    const ssize_t ignored = ::write(STDERR_FILENO, message.c_str(), message.size());
    (void)ignored;
    const ssize_t newline = ::write(STDERR_FILENO, "\n", 1);
    (void)newline;
    return false;
}

void tuneJavaMemoryIfNeeded(const std::vector<std::string>& command, std::int64_t memoryLimitBytes)
{
    if (command.empty() || command[0] != "java")
    {
        return;
    }
    if (memoryLimitBytes <= 0)
    {
        return;
    }

    // Java 在 RLIMIT_AS 下容易因为默认堆/元空间预留而启动失败，这里给它一个更保守的堆上限。
    const int memMb = static_cast<int>(memoryLimitBytes / (1024 * 1024));
    const int xmxMb = std::max(16, std::min(64, memMb / 4));
    const std::string opts =
        "-Xmx" + std::to_string(xmxMb) +
        "m -Xms16m -XX:ReservedCodeCacheSize=16m -XX:CompressedClassSpaceSize=16m -XX:MaxMetaspaceSize=96m";
    setenv("JAVA_TOOL_OPTIONS", opts.c_str(), 1);
}

}

SandboxRunResult SandboxRuntime::runProcess(const SandboxPreparedTask& task, const SandboxInstance& instance) const
{
    SandboxRunResult result;

    if (task.runner == nullptr)
    {
        result.status = JudgeStatus::SE;
        result.message = "运行阶段缺少 LanguageRunner。";
        return result;
    }

    FilesystemManager filesystemManager;
    std::string fsError;
    SandboxWorkspaceLayout runtimeLayout;
    if (!filesystemManager.prepareSandbox(instance, task, runtimeLayout, fsError))
    {
        result.status = JudgeStatus::SE;
        result.message = fsError;
        return result;
    }

    result.status = JudgeStatus::AC;

    for (std::size_t i = 0; i < task.testCases.size(); ++i)
    {
        SandboxCaseResult caseResult;
        caseResult.standardInput = readTextFile(task.testCases[i].inputPath);
        caseResult.standardOutput = readTextFile(task.testCases[i].outputPath);

        runtimeLayout.runStdoutPath = runtimeLayout.rootDir + "/" + std::to_string(i + 1) + ".out";
        runtimeLayout.runStderrPath = runtimeLayout.rootDir + "/" + std::to_string(i + 1) + ".err";

        NamespaceManager namespaceManager;
        CgroupManager cgroupManager;
        SeccompManager seccompManager;

        ProcessRunOptions options;
        options.command = task.runner->buildRunCommand(runtimeLayout);
        options.workingDirectory = runtimeLayout.workDir;
        options.stdinPath = task.testCases[i].inputPath;
        options.stdoutPath = runtimeLayout.runStdoutPath;
        options.stderrPath = runtimeLayout.runStderrPath;
        options.timeLimitMs = task.task.timeLimit;
        options.memoryLimitBytes = task.task.memoryLimit;
        options.processLimit = task.runner->processLimit();
        options.beforeExec = [&namespaceManager, &cgroupManager, &seccompManager, &task, &options]() {
            tuneJavaMemoryIfNeeded(options.command, task.task.memoryLimit);
            std::string message;
            if (!namespaceManager.apply(message))
            {
                return writeSandboxError(message);
            }
            if (!cgroupManager.apply(task.task.timeLimit, task.task.memoryLimit, message))
            {
                return writeSandboxError(message);
            }
            if (!seccompManager.apply(message))
            {
                return writeSandboxError(message);
            }
            return true;
        };

        ProcessRunResult processResult = runProcessCommand(options);
        caseResult.runTime = processResult.wallTimeMs;
        caseResult.runMemory = processResult.peakMemoryBytes;
        caseResult.personalOutput = readTextFile(runtimeLayout.runStdoutPath);
        const std::string runtimeError = readTextFile(runtimeLayout.runStderrPath);
        caseResult.status = detectCaseStatus(processResult, task.task.memoryLimit, runtimeError);

        // 非 SPJ：使用（去掉末尾空白后的）字符串比较。
        // SPJ：由 spj 返回值决定，不做预比较，避免误判。
        if (caseResult.status == JudgeStatus::AC && !task.hasSpj &&
            trimRight(caseResult.personalOutput) != trimRight(caseResult.standardOutput))
        {
            caseResult.status = JudgeStatus::WA;
        }

        if (caseResult.status == JudgeStatus::AC && task.hasSpj)
        {
            ProcessRunOptions spjOptions;
            spjOptions.command = {task.spjExecutablePath,
                                  task.testCases[i].inputPath,
                                  task.testCases[i].outputPath,
                                  runtimeLayout.runStdoutPath};
            spjOptions.timeLimitMs = std::max(1000, task.task.timeLimit);
            spjOptions.memoryLimitBytes = std::max<std::int64_t>(task.task.memoryLimit, 256LL * 1024 * 1024);
            spjOptions.stdoutPath = runtimeLayout.rootDir + "/" + std::to_string(i + 1) + ".spj.out";
            spjOptions.stderrPath = runtimeLayout.rootDir + "/" + std::to_string(i + 1) + ".spj.err";
            spjOptions.beforeExec = [&task]() {
                NamespaceManager namespaceManager;
                CgroupManager cgroupManager;
                SeccompManager seccompManager;

                std::string message;
                if (!namespaceManager.apply(message))
                {
                    return writeSandboxError(message);
                }
                if (!cgroupManager.apply(std::max(1000, task.task.timeLimit),
                                         std::max<std::int64_t>(task.task.memoryLimit, 256LL * 1024 * 1024),
                                         message))
                {
                    return writeSandboxError(message);
                }
                if (!seccompManager.apply(message))
                {
                    return writeSandboxError(message);
                }
                return true;
            };
            ProcessRunResult spjResult = runProcessCommand(spjOptions);
            if (spjResult.timedOut || spjResult.signalNumber != 0)
            {
                caseResult.status = JudgeStatus::SE;
            }
            else if (spjResult.exitCode != 0)
            {
                caseResult.status = JudgeStatus::WA;
            }
        }

        result.runTime = std::max(result.runTime, caseResult.runTime);
        result.runMemory = std::max(result.runMemory, caseResult.runMemory);
        result.caseResults.push_back(caseResult);

        if (caseResult.status != JudgeStatus::AC && result.status == JudgeStatus::AC)
        {
            result.status = caseResult.status;
            if (shouldAttachGlobalMessage(caseResult.status))
            {
                result.message = readTextFile(runtimeLayout.runStderrPath);
                if (result.message.empty())
                {
                    result.message = "运行失败";
                }
            }
        }
    }

    // 清理运行目录（测试时可通过环境变量跳过）
    if (std::getenv("XDOJ_KEEP_RUN_DIR") == nullptr)
    {
        std::error_code ec;
        std::filesystem::remove_all(runtimeLayout.rootDir, ec);
    }

    return result;
}
