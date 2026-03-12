#include "CRunner.hpp"

#include <vector>

std::string CRunner::languageKey() const
{
    return "C";
}

bool CRunner::requiresCompilation() const
{
    return true;
}

std::string CRunner::sourceFileName() const
{
    return "main.c";
}

std::string CRunner::executableFileName() const
{
    return "main";
}

int CRunner::processLimit() const
{
    return 8;
}

std::vector<std::string> CRunner::buildCompileCommand(const SandboxWorkspaceLayout& layout) const
{
    return {"gcc", layout.sourcePath, "-O2", "-std=c11", "-o", layout.executablePath};
}

std::vector<std::string> CRunner::buildRunCommand(const SandboxWorkspaceLayout& layout) const
{
    return {layout.executablePath};
}
