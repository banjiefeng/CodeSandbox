#pragma once

#include "../SandboxTypes.hpp"

#include <string>
#include <vector>

class LanguageRunner
{
public:
    virtual ~LanguageRunner() = default;

    virtual std::string languageKey() const = 0;
    virtual bool requiresCompilation() const = 0;
    virtual std::string sourceFileName() const = 0;
    virtual std::string executableFileName() const = 0;
    virtual int processLimit() const = 0;
    virtual std::vector<std::string> buildCompileCommand(const SandboxWorkspaceLayout& layout) const = 0;
    virtual std::vector<std::string> buildRunCommand(const SandboxWorkspaceLayout& layout) const = 0;
};
