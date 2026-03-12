#include "SeccompManager.hpp"

#include <seccomp.h>
#include <sys/prctl.h>

#include <cerrno>
#include <cstring>

bool SeccompManager::apply(std::string& error) const
{
    error.clear();
    if (prctl(PR_SET_NO_NEW_PRIVS, 1, 0, 0, 0) != 0)
    {
        error = "设置 no_new_privs 失败。";
        return false;
    }

    scmp_filter_ctx ctx = seccomp_init(SCMP_ACT_ALLOW);
    if (ctx == nullptr)
    {
        error = "seccomp_init 失败。";
        return false;
    }

    const int denyAction = SCMP_ACT_ERRNO(EPERM);
    const int denySyscalls[] = {
        SCMP_SYS(bpf),
        SCMP_SYS(kexec_load),
        SCMP_SYS(kexec_file_load),
        SCMP_SYS(perf_event_open),
        SCMP_SYS(ptrace),
        SCMP_SYS(reboot),
        SCMP_SYS(swapon),
        SCMP_SYS(swapoff),
        SCMP_SYS(syslog),
        SCMP_SYS(init_module),
        SCMP_SYS(finit_module),
        SCMP_SYS(delete_module),
        SCMP_SYS(mount),
        SCMP_SYS(umount2),
        SCMP_SYS(pivot_root),
        SCMP_SYS(setns),
        SCMP_SYS(unshare),
        SCMP_SYS(open_by_handle_at),
        SCMP_SYS(name_to_handle_at),
        SCMP_SYS(process_vm_readv),
        SCMP_SYS(process_vm_writev),
        SCMP_SYS(iopl),
        SCMP_SYS(ioperm),
        SCMP_SYS(keyctl),
        SCMP_SYS(add_key),
        SCMP_SYS(request_key),
    };

    for (int syscallNr : denySyscalls)
    {
        // 某些 syscall 在不同内核/架构下可能不存在，忽略失败以保证可用性。
        seccomp_rule_add(ctx, denyAction, syscallNr, 0);
    }

    if (seccomp_load(ctx) != 0)
    {
        error = std::string("seccomp_load 失败: ") + std::strerror(errno);
        seccomp_release(ctx);
        return false;
    }

    seccomp_release(ctx);
    return true;
}
