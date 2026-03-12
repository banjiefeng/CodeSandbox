#pragma once

#include "../SandboxTypes.hpp"

#include <condition_variable>
#include <memory>
#include <mutex>

class SandboxPool;

class SandboxWorkerPool
{
public:
    SandboxWorkerPool(int workerCount, const std::shared_ptr<SandboxPool>& sandboxPool);

    SandboxRunResult execute(const SandboxPreparedTask& task) const;

private:
    int workerCount;
    std::shared_ptr<SandboxPool> sandboxPool;
    mutable std::mutex mutex;
    mutable std::condition_variable condition;
    mutable int activeWorkers = 0;
};
