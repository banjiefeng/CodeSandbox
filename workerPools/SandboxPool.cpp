#include "SandboxPool.hpp"

#include "../SandboxTypes.hpp"

#include <filesystem>

namespace fs = std::filesystem;

SandboxPool::SandboxPool(std::size_t capacity)
{
    roots.reserve(capacity);
    inUse.assign(capacity, false);
    for (std::size_t i = 0; i < capacity; ++i)
    {
        const std::string root = (fs::path("/tmp/xdoj-sandbox/pool") / std::to_string(i)).string();
        std::error_code ec;
        fs::create_directories(root, ec);
        roots.push_back(root);
    }
}

bool SandboxPool::acquire(SandboxInstance& instance)
{
    std::unique_lock<std::mutex> lock(mutex);
    condition.wait(lock, [this]() {
        for (std::size_t i = 0; i < inUse.size(); ++i)
        {
            if (!inUse[i])
            {
                return true;
            }
        }
        return false;
    });

    for (std::size_t i = 0; i < inUse.size(); ++i)
    {
        if (!inUse[i])
        {
            inUse[i] = true;
            instance.slotId = i;
            instance.rootDir = roots[i];
            return true;
        }
    }
    return false;
}

void SandboxPool::release(std::size_t slotId)
{
    std::lock_guard<std::mutex> lock(mutex);
    if (slotId < inUse.size())
    {
        inUse[slotId] = false;
    }
    condition.notify_one();
}

std::size_t SandboxPool::capacity() const
{
    return roots.size();
}
