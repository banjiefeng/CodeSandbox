#include "CompilerPool.hpp"

#include "../languageRunners/LanguageRunner.hpp"
#include "../ProcessUtils.hpp"

#include <algorithm>
#include <filesystem>
#include <fstream>
#include <sstream>
#include <cstdlib>

namespace fs = std::filesystem;

namespace {

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

void tuneJavaToolOptionsIfNeeded(const std::vector<std::string>& command, std::int64_t memoryLimitBytes)
{
    if (command.empty())
    {
        return;
    }
    if (command[0] != "javac" && command[0] != "java")
    {
        return;
    }
    if (memoryLimitBytes <= 0)
    {
        return;
    }

    const int memMb = static_cast<int>(memoryLimitBytes / (1024 * 1024));
    const int xmxMb = std::max(16, std::min(64, memMb / 4));
    // 降低 code cache 预留，避免在较小 RLIMIT_AS 下 JVM 初始化直接失败。
    const std::string opts =
        "-Xmx" + std::to_string(xmxMb) +
        "m -Xms16m -XX:ReservedCodeCacheSize=16m -XX:CompressedClassSpaceSize=16m -XX:MaxMetaspaceSize=96m";
    setenv("JAVA_TOOL_OPTIONS", opts.c_str(), 1);
}

}

CompilerPool::CompilerPool(int workerCount) : workerCount(workerCount)
{
}

SandboxCompileResult CompilerPool::compile(SandboxPreparedTask& task) const
{
    std::unique_lock<std::mutex> slotLock(mutex);
    condition.wait(slotLock, [this]() { return activeWorkers < workerCount; });
    ++activeWorkers;
    slotLock.unlock();

    struct SlotGuard {
        const CompilerPool* pool;
        ~SlotGuard()
        {
            std::lock_guard<std::mutex> lock(pool->mutex);
            --pool->activeWorkers;
            pool->condition.notify_one();
        }
    } slotGuard{this};

    SandboxCompileResult result;

    if (task.task.code.empty())
    {
        result.success = false;
        result.message = "提交代码为空，无法编译。";
        return result;
    }

    if (task.runner == nullptr)
    {
        result.success = false;
        result.message = "缺少语言运行器，无法编译。";
        return result;
    }

    if (workerCount <= 0)
    {
        result.success = false;
        result.message = "CompilerPool 未初始化可用工作线程。";
        return result;
    }

    task.buildLayout.rootDir = (fs::path("/tmp/xdoj-sandbox/build") / task.task.submitId).string();
    task.buildLayout.workDir = task.buildLayout.rootDir;
    task.buildLayout.sourcePath = (fs::path(task.buildLayout.workDir) / task.runner->sourceFileName()).string();
    task.buildLayout.executablePath = (fs::path(task.buildLayout.workDir) / task.runner->executableFileName()).string();
    task.buildLayout.compileStdoutPath = (fs::path(task.buildLayout.workDir) / "compile.stdout").string();
    task.buildLayout.compileStderrPath = (fs::path(task.buildLayout.workDir) / "compile.stderr").string();

    std::error_code ec;
    fs::remove_all(task.buildLayout.rootDir, ec);
    ec.clear();
    fs::create_directories(task.buildLayout.workDir, ec);
    if (ec)
    {
        result.success = false;
        result.message = "无法创建编译工作目录: " + ec.message();
        return result;
    }

    std::ofstream sourceFile(task.buildLayout.sourcePath.c_str(), std::ios::out | std::ios::binary);
    if (!sourceFile.is_open())
    {
        result.success = false;
        result.message = "无法写入源代码文件。";
        return result;
    }
    sourceFile << task.task.code;
    sourceFile.close();

    if (!task.runner->requiresCompilation())
    {
        result.message.clear();
        return result;
    }

    const std::vector<std::string> compileCommand = task.runner->buildCompileCommand(task.buildLayout);
    ProcessRunOptions options;
    options.command = compileCommand;
    options.workingDirectory = task.buildLayout.workDir;
    options.stdoutPath = task.buildLayout.compileStdoutPath;
    options.stderrPath = task.buildLayout.compileStderrPath;
    options.timeLimitMs = std::max(1000, task.task.timeLimit * 2);
    options.memoryLimitBytes = std::max<std::int64_t>(task.task.memoryLimit, 256LL * 1024 * 1024);
    if (!options.command.empty() && options.command[0] == "javac")
    {
        // javac 本身不是用户代码执行，编译阶段关闭 RLIMIT_AS，避免 JVM 初始化失败。
        options.memoryLimitBytes = 0;
    }
    options.beforeExec = [&options]() {
        tuneJavaToolOptionsIfNeeded(options.command, options.memoryLimitBytes);
        return true;
    };
    ProcessRunResult compileProcess = runProcessCommand(options);
    if (compileProcess.timedOut)
    {
        result.success = false;
        result.message = "编译超时。";
        return result;
    }

    if (compileProcess.exitCode != 0 || compileProcess.signalNumber != 0)
    {
        result.success = false;
        result.message = readTextFile(task.buildLayout.compileStderrPath);
        if (result.message.empty())
        {
            result.message = readTextFile(task.buildLayout.compileStdoutPath);
        }
        if (result.message.empty())
        {
            result.message = "编译失败，编译器未返回详细信息。";
        }
        return result;
    }

    result.message.clear();
    return result;
}
