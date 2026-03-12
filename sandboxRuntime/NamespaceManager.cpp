#include "NamespaceManager.hpp"

#include <fcntl.h>
#include <sched.h>
#include <sys/mount.h>
#include <sys/types.h>
#include <unistd.h>

#include <cerrno>
#include <cstring>
#include <fstream>

namespace {

bool useStrictMode()
{
    const char* strict = std::getenv("XDOJ_NAMESPACE_STRICT");
    return strict != nullptr && std::string(strict) == "1";
}

bool writeFile(const std::string& path, const std::string& content, std::string* errorMessage = nullptr)
{
    std::ofstream out(path.c_str(), std::ios::out | std::ios::binary);
    if (!out.is_open())
    {
        if (errorMessage != nullptr)
        {
            *errorMessage = "打开 " + path + " 失败: " + std::strerror(errno);
        }
        return false;
    }
    out << content;
    if (!out.good())
    {
        if (errorMessage != nullptr)
        {
            *errorMessage = "写入 " + path + " 失败: " + std::strerror(errno);
        }
        return false;
    }
    return true;
}

}

bool NamespaceManager::apply(std::string& warning) const
{
    warning.clear();
    const uid_t uid = getuid();
    const gid_t gid = getgid();

    if (unshare(CLONE_NEWUSER | CLONE_NEWNS | CLONE_NEWIPC | CLONE_NEWUTS | CLONE_NEWNET) != 0)
    {
        warning = std::string("unshare namespace 失败: ") + std::strerror(errno);
        return !useStrictMode();
    }

    std::string errorMessage;
    if (!writeFile("/proc/self/setgroups", "deny", &errorMessage))
    {
        // 某些内核/容器环境不允许写 setgroups，继续尝试 uid/gid 映射。
        if (errno != ENOENT && errno != EPERM && errno != EACCES)
        {
            warning = errorMessage;
            return false;
        }
    }
    if (!writeFile("/proc/self/uid_map", "0 " + std::to_string(uid) + " 1\n", &errorMessage))
    {
        warning = errorMessage;
        return !useStrictMode();
    }
    if (!writeFile("/proc/self/gid_map", "0 " + std::to_string(gid) + " 1\n", &errorMessage))
    {
        // 某些环境只允许 uid 映射。为了保证模块可用性，gid 映射失败时降级运行。
        if (errno != EPERM && errno != EACCES)
        {
            warning = errorMessage;
            return false;
        }
    }

    if (setgid(0) != 0 && errno != EPERM)
    {
        warning = std::string("setgid 失败: ") + std::strerror(errno);
        return false;
    }
    if (setuid(0) != 0 && errno != EPERM)
    {
        warning = std::string("setuid 失败: ") + std::strerror(errno);
        return false;
    }

    if (mount(nullptr, "/", nullptr, MS_REC | MS_PRIVATE, nullptr) != 0)
    {
        warning = std::string("设置 mount propagation 失败: ") + std::strerror(errno);
        return !useStrictMode();
    }

    sethostname("xdoj-sandbox", 12);
    return true;
}
