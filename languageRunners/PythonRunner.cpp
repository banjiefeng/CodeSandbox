#include "PythonRunner.hpp"

#include <vector>

std::string PythonRunner::languageKey() const
{
    return "Python3";
}

bool PythonRunner::requiresCompilation() const
{
    return true;
}

std::string PythonRunner::sourceFileName() const
{
    return "main.py";
}

std::string PythonRunner::executableFileName() const
{
    return "main.py";
}

int PythonRunner::processLimit() const
{
    return 32;
}

std::vector<std::string> PythonRunner::buildCompileCommand(const SandboxWorkspaceLayout& layout) const
{
    return {"python3", "-m", "py_compile", layout.sourcePath};
}

std::vector<std::string> PythonRunner::buildRunCommand(const SandboxWorkspaceLayout& layout) const
{
    return {"python3", layout.sourcePath};
}
