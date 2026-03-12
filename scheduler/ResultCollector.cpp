#include "ResultCollector.hpp"

Json::Value ResultCollector::collectResult(const SandboxTask& task,
                                          const SandboxCompileResult& compileResult,
                                          const SandboxRunResult& runResult) const
{
    SandboxFinalResult finalResult;
    finalResult.task = task;
    finalResult.status = compileResult.success ? runResult.status : JudgeStatus::CE;
    finalResult.runTime = runResult.runTime;
    finalResult.runMemory = runResult.runMemory;
    finalResult.length = static_cast<int>(task.code.size());
    if (!compileResult.success)
    {
        finalResult.compilerInfo = compileResult.message;
    }
    else if (runResult.status == JudgeStatus::SE)
    {
        finalResult.compilerInfo = runResult.message;
    }
    else
    {
        finalResult.compilerInfo.clear();
    }
    finalResult.testInfo = runResult.caseResults;
    return finalResult.toJson();
}

Json::Value ResultCollector::collectSystemError(const SandboxTask& task, const std::string& message) const
{
    SandboxCompileResult compileResult;
    compileResult.success = true;
    compileResult.message = message;

    SandboxRunResult runResult;
    runResult.status = JudgeStatus::SE;
    runResult.message = message;
    return collectResult(task, compileResult, runResult);
}
