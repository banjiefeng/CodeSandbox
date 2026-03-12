#pragma once

#include "../SandboxTypes.hpp"
#include "CompileQueue.hpp"
#include "ResultCollector.hpp"
#include "RunQueue.hpp"

#include <memory>
#include <string>
#include <unordered_map>

class CompilerPool;
class LanguageRunner;
class SandboxPool;
class SandboxWorkerPool;

class TaskManager
{
public:
    TaskManager(const std::shared_ptr<SandboxPool>& sandboxPool,
                const std::shared_ptr<CompilerPool>& compilerPool);
    ~TaskManager();

    void registerRunner(const std::string& language, std::unique_ptr<LanguageRunner> runner);
    Json::Value processTask(const SandboxTask& task);

private:
    const LanguageRunner* findRunner(const std::string& language) const;
    std::string validateTask(const SandboxTask& task) const;
    bool loadTestCases(const SandboxTask& task,
                       std::vector<SandboxTestCase>& testCases,
                       std::string& errorMessage) const;

    CompileQueue compileQueue;
    RunQueue runQueue;
    ResultCollector resultCollector;
    std::shared_ptr<SandboxPool> sandboxPool;
    std::shared_ptr<CompilerPool> compilerPool;
    std::unique_ptr<SandboxWorkerPool> sandboxWorkerPool;
    std::unordered_map<std::string, std::unique_ptr<LanguageRunner>> runners;
};
