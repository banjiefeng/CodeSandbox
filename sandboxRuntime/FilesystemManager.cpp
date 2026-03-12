#include "FilesystemManager.hpp"

#include <filesystem>

namespace fs = std::filesystem;

bool FilesystemManager::prepareSandbox(const SandboxInstance& instance,
                                       const SandboxPreparedTask& task,
                                       SandboxWorkspaceLayout& runtimeLayout,
                                       std::string& error) const
{
    error.clear();

    runtimeLayout.rootDir = (fs::path(instance.rootDir) / "sandbox" / task.task.submitId).string();
    runtimeLayout.workDir = (fs::path(runtimeLayout.rootDir) / "work").string();
    runtimeLayout.rootfsDir = (fs::path(runtimeLayout.rootDir) / "rootfs").string();
    runtimeLayout.compileStdoutPath = (fs::path(runtimeLayout.rootDir) / "compile.stdout").string();
    runtimeLayout.compileStderrPath = (fs::path(runtimeLayout.rootDir) / "compile.stderr").string();

    std::error_code ec;
    fs::remove_all(runtimeLayout.rootDir, ec);
    ec.clear();
    fs::create_directories(runtimeLayout.workDir, ec);
    if (ec)
    {
        error = "创建沙箱工作目录失败: " + ec.message();
        return false;
    }

    fs::create_directories(runtimeLayout.rootfsDir, ec);
    if (ec)
    {
        error = "创建 rootfs 目录失败: " + ec.message();
        return false;
    }

    for (fs::directory_iterator it(task.buildLayout.workDir, ec); !ec && it != fs::directory_iterator(); ++it)
    {
        const fs::path destination = fs::path(runtimeLayout.workDir) / it->path().filename();
        fs::copy(it->path(),
                 destination,
                 fs::copy_options::recursive | fs::copy_options::overwrite_existing,
                 ec);
        if (ec)
        {
            error = "复制编译产物到沙箱目录失败: " + ec.message();
            return false;
        }
    }
    if (ec)
    {
        error = "遍历编译目录失败: " + ec.message();
        return false;
    }

    runtimeLayout.sourcePath = (fs::path(runtimeLayout.workDir) /
        fs::path(task.buildLayout.sourcePath).filename()).string();
    runtimeLayout.executablePath = (fs::path(runtimeLayout.workDir) /
        fs::path(task.buildLayout.executablePath).filename()).string();
    return true;
}
