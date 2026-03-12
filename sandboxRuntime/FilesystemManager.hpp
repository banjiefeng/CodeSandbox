#pragma once

#include "../SandboxTypes.hpp"

#include <string>

class FilesystemManager
{
public:
    bool prepareSandbox(const SandboxInstance& instance,
                        const SandboxPreparedTask& task,
                        SandboxWorkspaceLayout& runtimeLayout,
                        std::string& error) const;
};
