#pragma once

#include <cstddef>
#include <condition_variable>
#include <mutex>
#include <string>
#include <vector>

class SandboxPool
{
public:
    explicit SandboxPool(std::size_t capacity);

    bool acquire(struct SandboxInstance& instance);
    void release(std::size_t slotId);
    std::size_t capacity() const;

private:
    mutable std::mutex mutex;
    std::condition_variable condition;
    std::vector<std::string> roots;
    std::vector<bool> inUse;
};
