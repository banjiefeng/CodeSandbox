#pragma once

#include "../SandboxTypes.hpp"

class SandboxRuntime
{
public:
    SandboxRunResult runProcess(const SandboxPreparedTask& task, const SandboxInstance& instance) const;
};
