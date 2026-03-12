#include "TaskManager.hpp"

#include "../ProcessUtils.hpp"
#include "../languageRunners/LanguageRunner.hpp"
#include "../workerPools/CompilerPool.hpp"
#include "../workerPools/SandboxPool.hpp"
#include "../workerPools/SandboxWorkerPool.hpp"

#include <filesystem>
#include <cstdlib>
#include <mutex>

namespace fs = std::filesystem;

namespace {

std::mutex gSpjMutex;

struct BuildDirGuard
{
    std::string path;

    ~BuildDirGuard()
    {
        if (path.empty() || std::getenv("XDOJ_KEEP_BUILD_DIR") != nullptr)
        {
            return;
        }
        std::error_code ec;
        fs::remove_all(path, ec);
    }
};

SandboxCompileResult compileSpjIfNeeded(const std::string& spjSource,
                                       const std::string& spjExecutable,
                                       int timeLimitMs,
                                       std::int64_t memoryLimitBytes,
                                       std::string& errorMessage)
{
    errorMessage.clear();

    std::error_code ec;
    if (!fs::exists(spjSource, ec))
    {
        return SandboxCompileResult{true, ""};
    }

    fs::create_directories(fs::path(spjExecutable).parent_path(), ec);
    if (ec)
    {
        errorMessage = "创建 SPJ 目录失败: " + ec.message();
        return SandboxCompileResult{false, errorMessage};
    }

    bool needBuild = true;
    if (fs::exists(spjExecutable, ec))
    {
        std::error_code ec2;
        const auto srcTime = fs::last_write_time(spjSource, ec2);
        const auto exeTime = fs::last_write_time(spjExecutable, ec2);
        if (!ec2 && exeTime >= srcTime)
        {
            needBuild = false;
        }
    }

    if (!needBuild)
    {
        return SandboxCompileResult{true, ""};
    }

    // 复用 ProcessUtils 的方式编译 SPJ（可信代码，但仍做资源限制）
    ProcessRunOptions options;
    options.command = {"g++", spjSource, "-O2", "-std=c++17", "-o", spjExecutable};
    options.timeLimitMs = std::max(1000, timeLimitMs);
    options.memoryLimitBytes = std::max<std::int64_t>(memoryLimitBytes, 256LL * 1024 * 1024);

    ProcessRunResult pr = runProcessCommand(options);
    if (pr.timedOut || pr.exitCode != 0 || pr.signalNumber != 0)
    {
        errorMessage = "SPJ 编译失败。";
        return SandboxCompileResult{false, errorMessage};
    }

    return SandboxCompileResult{true, ""};
}

}

TaskManager::TaskManager(const std::shared_ptr<SandboxPool>& sandboxPool,
                         const std::shared_ptr<CompilerPool>& compilerPool)
    : sandboxPool(sandboxPool), compilerPool(compilerPool)
{
    const int workerCount = sandboxPool ? static_cast<int>(sandboxPool->capacity()) : 0;
    sandboxWorkerPool = std::make_unique<SandboxWorkerPool>(workerCount, sandboxPool);
}

TaskManager::~TaskManager() = default;

void TaskManager::registerRunner(const std::string& language, std::unique_ptr<LanguageRunner> runner)
{
    runners[language] = std::move(runner);
}

Json::Value TaskManager::processTask(const SandboxTask& task)
{
    const std::string validationError = validateTask(task);
    if (!validationError.empty())
    {
        return resultCollector.collectSystemError(task, validationError);
    }

    const LanguageRunner* runner = findRunner(task.language);
    if (runner == nullptr)
    {
        return resultCollector.collectSystemError(task, "当前语言没有对应的 LanguageRunner。");
    }

    std::vector<SandboxTestCase> testCases;
    std::string loadErrorMessage;
    if (!loadTestCases(task, testCases, loadErrorMessage))
    {
        return resultCollector.collectSystemError(task, loadErrorMessage);
    }

    SandboxPreparedTask preparedTask;
    preparedTask.task = task;
    preparedTask.runner = runner;
    preparedTask.testCases = testCases;
    BuildDirGuard buildDirGuard;

    {
        // 初始化阶段: 检查并编译 SPJ（如果存在）
        preparedTask.spjSourcePath = (fs::path(task.dataPath) / "spj.cpp").string();
        preparedTask.spjExecutablePath = (fs::path("/tmp/xdoj-sandbox/spj") / task.problemId / "spj").string();
        std::lock_guard<std::mutex> lock(gSpjMutex);
        std::string spjError;
        SandboxCompileResult spjCompileResult =
            compileSpjIfNeeded(preparedTask.spjSourcePath,
                               preparedTask.spjExecutablePath,
                               task.timeLimit,
                               task.memoryLimit,
                               spjError);
        preparedTask.hasSpj = spjCompileResult.success && fs::exists(preparedTask.spjSourcePath);
        if (!spjCompileResult.success)
        {
            return resultCollector.collectSystemError(task, spjError);
        }
    }

    SandboxCompileResult compileResult = compilerPool->compile(preparedTask);
    buildDirGuard.path = preparedTask.buildLayout.rootDir;
    if (!compileResult.success)
    {
        SandboxRunResult emptyRunResult;
        emptyRunResult.status = JudgeStatus::CE;
        emptyRunResult.message = compileResult.message;
        return resultCollector.collectResult(task, compileResult, emptyRunResult);
    }

    SandboxRunResult runResult = sandboxWorkerPool->execute(preparedTask);
    return resultCollector.collectResult(task, compileResult, runResult);
}

const LanguageRunner* TaskManager::findRunner(const std::string& language) const
{
    std::unordered_map<std::string, std::unique_ptr<LanguageRunner>>::const_iterator it = runners.find(language);
    if (it == runners.end())
    {
        return nullptr;
    }
    return it->second.get();
}

std::string TaskManager::validateTask(const SandboxTask& task) const
{
    if (task.submitId.empty())
    {
        return "SubmitId 不能为空。";
    }
    if (task.problemId.empty())
    {
        return "ProblemId 不能为空。";
    }
    if (task.language.empty())
    {
        return "Language 不能为空。";
    }
    if (task.code.empty())
    {
        return "Code 不能为空。";
    }
    if (task.testNum <= 0)
    {
        return "JudgeNum 必须大于 0。";
    }
    if (task.timeLimit <= 0)
    {
        return "TimeLimit 必须大于 0。";
    }
    if (task.memoryLimit <= 0)
    {
        return "MemoryLimit 必须大于 0。";
    }
    if (!compilerPool)
    {
        return "CompilerPool 未初始化。";
    }
    if (!sandboxPool)
    {
        return "SandboxPool 未初始化。";
    }
    return "";
}

bool TaskManager::loadTestCases(const SandboxTask& task,
                                std::vector<SandboxTestCase>& testCases,
                                std::string& errorMessage) const
{
    if (!fs::exists(task.dataPath))
    {
        errorMessage = "题目测试数据目录不存在: " + task.dataPath;
        return false;
    }

    for (int i = 1; i <= task.testNum; ++i)
    {
        fs::path inputPath = fs::path(task.dataPath) / (std::to_string(i) + ".in");
        fs::path outputPath = fs::path(task.dataPath) / (std::to_string(i) + ".out");
        if (!fs::exists(inputPath) || !fs::exists(outputPath))
        {
            errorMessage = "缺少测试点文件: " + inputPath.string() + " 或 " + outputPath.string();
            return false;
        }

        SandboxTestCase testCase;
        testCase.index = i;
        testCase.inputPath = inputPath.string();
        testCase.outputPath = outputPath.string();
        testCases.push_back(testCase);
    }

    return true;
}
