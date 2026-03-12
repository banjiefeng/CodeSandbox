#pragma once

#include <cstdint>
#include <string>

class CgroupManager
{
public:
    bool apply(int timeLimitMs, std::int64_t memoryLimitBytes, std::string& error) const;
};
