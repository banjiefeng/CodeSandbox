#pragma once

#include "../SandboxTypes.hpp"

#include <condition_variable>
#include <mutex>

class CompilerPool
{
public:
    explicit CompilerPool(int workerCount);

    SandboxCompileResult compile(SandboxPreparedTask& task) const;

private:
    int workerCount;
    mutable std::mutex mutex;
    mutable std::condition_variable condition;
    mutable int activeWorkers = 0;
};
