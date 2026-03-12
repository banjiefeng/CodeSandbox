#include "CompileQueue.hpp"

void CompileQueue::pushTask(const SandboxPreparedTask& task)
{
    std::lock_guard<std::mutex> lock(mutex);
    tasks.push(task);
    condition.notify_one();
}

bool CompileQueue::popTask(SandboxPreparedTask& task)
{
    std::unique_lock<std::mutex> lock(mutex);
    condition.wait(lock, [this]() { return !tasks.empty(); });
    if (tasks.empty())
    {
        return false;
    }
    task = tasks.front();
    tasks.pop();
    return true;
}
