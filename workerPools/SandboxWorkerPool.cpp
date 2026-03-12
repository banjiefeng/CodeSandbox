#include "SandboxWorkerPool.hpp"

#include "../sandboxRuntime/SandboxRuntime.hpp"
#include "SandboxPool.hpp"

SandboxWorkerPool::SandboxWorkerPool(int workerCount, const std::shared_ptr<SandboxPool>& sandboxPool)
    : workerCount(workerCount), sandboxPool(sandboxPool)
{
}

SandboxRunResult SandboxWorkerPool::execute(const SandboxPreparedTask& task) const
{
    std::unique_lock<std::mutex> slotLock(mutex);
    condition.wait(slotLock, [this]() { return activeWorkers < workerCount; });
    ++activeWorkers;
    slotLock.unlock();

    struct SlotGuard {
        const SandboxWorkerPool* pool;
        ~SlotGuard()
        {
            std::lock_guard<std::mutex> lock(pool->mutex);
            --pool->activeWorkers;
            pool->condition.notify_one();
        }
    } slotGuard{this};

    SandboxRunResult result;
    if (workerCount <= 0)
    {
        result.status = JudgeStatus::SE;
        result.message = "SandboxWorkerPool 未初始化可用工作线程。";
        return result;
    }

    SandboxInstance instance;
    if (!sandboxPool || !sandboxPool->acquire(instance))
    {
        result.status = JudgeStatus::SE;
        result.message = "没有可用的 Sandbox 实例。";
        return result;
    }

    SandboxRuntime runtime;
    result = runtime.runProcess(task, instance);
    sandboxPool->release(instance.slotId);
    return result;
}
