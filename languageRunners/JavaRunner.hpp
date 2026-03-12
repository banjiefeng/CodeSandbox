#pragma once
#include "LanguageRunner.hpp"

class JavaRunner : public LanguageRunner
{
public:
    std::string languageKey() const override;
    bool requiresCompilation() const override;
    std::string sourceFileName() const override;
    std::string executableFileName() const override;
    int processLimit() const override;
    std::vector<std::string> buildCompileCommand(const SandboxWorkspaceLayout& layout) const override;
    std::vector<std::string> buildRunCommand(const SandboxWorkspaceLayout& layout) const override;
};
