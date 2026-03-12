#include "SandboxService.hpp"

#include "../languageRunners/CRunner.hpp"
#include "../languageRunners/CppRunner.hpp"
#include "../languageRunners/GoRunner.hpp"
#include "../languageRunners/JavaRunner.hpp"
#include "../languageRunners/PythonRunner.hpp"

#include <cstdlib>
#include <jsoncpp/json/json.h>

namespace {

std::string asTaskString(const Json::Value& value)
{
    if (value.isString())
    {
        return value.asString();
    }
    if (value.isInt() || value.isUInt() || value.isInt64() || value.isUInt64())
    {
        return std::to_string(value.asLargestInt());
    }
    return "";
}

Json::Value buildSystemError(const Json::Value& task, const std::string& message)
{
    SandboxTask sandboxTask;
    sandboxTask.submitId = asTaskString(task["SubmitId"]);
    sandboxTask.problemId = asTaskString(task["ProblemId"]);
    sandboxTask.code = task["Code"].asString();

    SandboxFinalResult result;
    result.task = sandboxTask;
    result.status = JudgeStatus::SE;
    result.length = static_cast<int>(sandboxTask.code.size());
    result.compilerInfo = message;
    return result.toJson();
}

}

SandboxService::SandboxService() {
    sandboxPool = std::make_shared<SandboxPool>(10);
    compilerPool = std::make_shared<CompilerPool>(5);
    taskManager = std::make_unique<TaskManager>(sandboxPool, compilerPool);

    taskManager->registerRunner("C", std::make_unique<CRunner>());
    taskManager->registerRunner("C++", std::make_unique<CppRunner>());
    taskManager->registerRunner("Go", std::make_unique<GoRunner>());
    taskManager->registerRunner("Java", std::make_unique<JavaRunner>());
    taskManager->registerRunner("Python3", std::make_unique<PythonRunner>());
}

SandboxService* SandboxService::GetInstance()
{
    return getInstance();
}

SandboxService* SandboxService::getInstance()
{
    static SandboxService instance;
    return &instance;
}

Json::Value SandboxService::submitTask(const Json::Value& task) {
    if (!task.isObject())
    {
        return buildSystemError(task, "submitTask 只接受 JSON object。");
    }

    SandboxTask sandboxTask;
    sandboxTask.submitId = asTaskString(task["SubmitId"]);
    sandboxTask.problemId = asTaskString(task["ProblemId"]);
    sandboxTask.code = task["Code"].asString();
    sandboxTask.language = task["Language"].asString();
    sandboxTask.timeLimit = task["TimeLimit"].asInt();
    sandboxTask.memoryLimit = task["MemoryLimit"].asInt64() * 1024 * 1024;
    sandboxTask.testNum = task["JudgeNum"].asInt();

    if (sandboxTask.submitId.empty() || sandboxTask.problemId.empty())
    {
        return buildSystemError(task, "submitTask 缺少 SubmitId 或 ProblemId。");
    }

    sandboxTask.dataPath = getProblemDataPath(sandboxTask.problemId);

    return taskManager->processTask(sandboxTask);
}

std::string SandboxService::getProblemDataPath(const std::string& problemId) const {
    const char* root = std::getenv("XDOJ_PROBLEMDATA_ROOT");
    if (root != nullptr && *root != '\0')
    {
        std::string base(root);
        if (!base.empty() && base.back() != '/')
        {
            base.push_back('/');
        }
        return base + problemId + "/";
    }

    return "../../problemdata/" + problemId + "/";
}

SandboxService::~SandboxService() = default;
