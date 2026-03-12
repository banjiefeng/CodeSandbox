#pragma once

#include "../SandboxTypes.hpp"

class ResultCollector
{
public:
    Json::Value collectResult(const SandboxTask& task,
                              const SandboxCompileResult& compileResult,
                              const SandboxRunResult& runResult) const;

    Json::Value collectSystemError(const SandboxTask& task, const std::string& message) const;
};
