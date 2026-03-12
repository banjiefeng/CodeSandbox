#pragma once

#include "../scheduler/TaskManager.hpp"
#include "../workerPools/CompilerPool.hpp"
#include "../workerPools/SandboxPool.hpp"

#include <jsoncpp/json/json.h>

#include <memory>
#include <string>

class SandboxService {
public:
    static SandboxService* GetInstance();

    static SandboxService* getInstance();

    SandboxService(const SandboxService&) = delete;
    SandboxService& operator=(const SandboxService&) = delete;

    Json::Value submitTask(const Json::Value& task);
    std::string getProblemDataPath(const std::string& problemId) const;

private:
    SandboxService();
    ~SandboxService();

    std::unique_ptr<TaskManager> taskManager;
    std::shared_ptr<SandboxPool> sandboxPool;
    std::shared_ptr<CompilerPool> compilerPool;
};
