#include "JavaRunner.hpp"

#include <vector>

std::string JavaRunner::languageKey() const
{
    return "Java";
}

bool JavaRunner::requiresCompilation() const
{
    return true;
}

std::string JavaRunner::sourceFileName() const
{
    return "Main.java";
}

std::string JavaRunner::executableFileName() const
{
    return "Main.class";
}

int JavaRunner::processLimit() const
{
    return 0;
}

std::vector<std::string> JavaRunner::buildCompileCommand(const SandboxWorkspaceLayout& layout) const
{
    return {"javac", layout.sourcePath};
}

std::vector<std::string> JavaRunner::buildRunCommand(const SandboxWorkspaceLayout& layout) const
{
    return {"java", "-cp", layout.workDir, "Main"};
}
