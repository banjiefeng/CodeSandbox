#pragma once

#include "../SandboxTypes.hpp"

#include <condition_variable>
#include <mutex>
#include <queue>

class RunQueue
{
public:
    void pushTask(const SandboxPreparedTask& task);
    bool popTask(SandboxPreparedTask& task);

private:
    mutable std::mutex mutex;
    std::condition_variable condition;
    std::queue<SandboxPreparedTask> tasks;
};
