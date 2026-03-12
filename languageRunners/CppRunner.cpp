#include "CppRunner.hpp"

#include <vector>

std::string CppRunner::languageKey() const
{
    return "C++";
}

bool CppRunner::requiresCompilation() const
{
    return true;
}

std::string CppRunner::sourceFileName() const
{
    return "main.cpp";
}

std::string CppRunner::executableFileName() const
{
    return "main";
}

int CppRunner::processLimit() const
{
    return 8;
}

std::vector<std::string> CppRunner::buildCompileCommand(const SandboxWorkspaceLayout& layout) const
{
    return {"g++", layout.sourcePath, "-O2", "-std=c++17", "-o", layout.executablePath};
}

std::vector<std::string> CppRunner::buildRunCommand(const SandboxWorkspaceLayout& layout) const
{
    return {layout.executablePath};
}
