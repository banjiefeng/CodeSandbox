#include "GoRunner.hpp"

#include <vector>

std::string GoRunner::languageKey() const
{
    return "Go";
}

bool GoRunner::requiresCompilation() const
{
    return true;
}

std::string GoRunner::sourceFileName() const
{
    return "main.go";
}

std::string GoRunner::executableFileName() const
{
    return "main";
}

int GoRunner::processLimit() const
{
    return 32;
}

std::vector<std::string> GoRunner::buildCompileCommand(const SandboxWorkspaceLayout& layout) const
{
    return {"go", "build", "-o", layout.executablePath, layout.sourcePath};
}

std::vector<std::string> GoRunner::buildRunCommand(const SandboxWorkspaceLayout& layout) const
{
    return {layout.executablePath};
}
