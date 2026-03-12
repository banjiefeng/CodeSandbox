# CodeSandbox 设计文档（XDOJ）

本文档描述 `src/CodeSandbox/` 模块的职责边界、关键流程、并发与隔离策略，以及对外返回 JSON 协议的稳定性约束。该模块对外唯一推荐入口为：

`SandboxService::GetInstance()->submitTask(runjson)`

## 1. 总体目标与约束

- 目标：在高并发提交下稳定编译与运行用户程序，对每个测试点产生可解释的判题结果，并汇总为统一 JSON 返回。
- 安全：每个用户程序执行进程尽力使用 `Namespace + Cgroups + seccomp` 三层隔离/限制；当宿主环境不支持某些 Namespace 能力时，默认采用“尽力而为”的兼容模式，线上可通过严格模式强制要求隔离成功。
- 架构约束：不改变外部调用链与数据协议，仅在 `CodeSandbox` 内部完善实现与健壮性。

## 2. 数据协议（返回 JSON 必须稳定）

### 2.1 状态码

见 [SandboxTypes.hpp](/home/jinzheyu/sandboxSet/sandbox.0.2/XDOJ/src/CodeSandbox/SandboxTypes.hpp)：

- `CE=1` 编译错误
- `AC=2` 通过
- `WA=3` 答案错误
- `RE=4` 运行时错误
- `TLE=5` 超时
- `MLE=6` 超内存
- `SE=7` 系统错误

### 2.2 顶层字段

`SandboxFinalResult::toJson()` 保证始终包含以下字段：

- `SubmitId` (string)
- `Status` (int)
- `ComplierInfo` (string)
- `RunTime` (string，形如 `"156MS"`)
- `RunMemory` (string，形如 `"12MB"`)
- `Length` (string，形如 `"2345B"`)
- `TestInfo` (array)

约定：

- `ComplierInfo` 语义：只用于 `CE`（编译器输出）与 `SE`（系统错误信息）；`WA/AC/RE/TLE/MLE` 默认返回空字符串，避免把“运行失败”之类的信息误当成编译信息。

### 2.3 TestInfo 字段稳定性

每个测试点条目必须包含以下字段（即使为空也必须存在）：

- `Status` (int)
- `RunTime` (string，`*MS`)
- `RunMemory` (string，`*MB`)
- `StandardInput` (string)
- `StandardOutput` (string)
- `PersonalOutput` (string)

## 3. 执行流程设计

### 3.1 初始化阶段（TaskManager）

入口：`TaskManager::processTask(const SandboxTask&)`

- 校验 `SubmitId/ProblemId/Language/Code/JudgeNum/TimeLimit/MemoryLimit`。
- 解析数据路径 `dataPath`：由 `SandboxService::getProblemDataPath()` 决定，可通过环境变量 `XDOJ_PROBLEMDATA_ROOT` 覆盖默认 `../../problemdata/<ProblemId>/`。
- 加载测试点：按 `1..JudgeNum` 读取 `<index>.in/.out` 文件存在性并构造 `SandboxTestCase` 列表。
- SPJ 初始化：若 `dataPath/spj.cpp` 存在，则在全局互斥 `gSpjMutex` 下编译到 `/tmp/xdoj-sandbox/spj/<ProblemId>/spj`，并在运行阶段启用 SPJ。

设计要点：

- **策略模式**：语言差异通过 `LanguageRunner` 多态实现（`C/Cpp/Go/Java/Python3`）。
- **最小可信计算**：SPJ 是题目侧代码，默认视为“相对可信”，但运行时同样放入 Namespace/Cgroup/seccomp 约束降低风险。

### 3.2 编译阶段（CompilerPool）

入口：`CompilerPool::compile(SandboxPreparedTask&)`

- 工作目录：`/tmp/xdoj-sandbox/build/<SubmitId>/`
- 写入源码：`<workDir>/<sourceFileName>`（如 `main.cpp`）
- 调用编译命令：`LanguageRunner::buildCompileCommand()`，输出重定向到 `compile.stdout/compile.stderr`。
- 返回 `SandboxCompileResult`：
  - 失败则 `success=false` 且 `message` 为编译器输出（尽量读取 stderr，其次 stdout）。

并发控制：

- **计数信号量模型**：`CompilerPool` 使用 `activeWorkers + condition_variable` 控制同一时间可并发的编译数量，避免爆 CPU/内存。

资源回收：

- `TaskManager` 内部使用 RAII（`BuildDirGuard`）在任务结束后默认清理 build 目录，可通过 `XDOJ_KEEP_BUILD_DIR=1` 保留。

### 3.3 运行阶段（SandboxWorkerPool + SandboxRuntime）

入口：`SandboxWorkerPool::execute()` 获取一个 `SandboxInstance`（槽位）后调用 `SandboxRuntime::runProcess()`。

`SandboxPool`：

- **对象池模式**：预创建 `/tmp/xdoj-sandbox/pool/<slot>`，高并发时通过 `acquire/release` 复用槽位，避免频繁创建大目录造成抖动。

`FilesystemManager`：

- 将编译产物复制到运行工作区：`<slotRoot>/sandbox/<SubmitId>/work/`

每个测试点执行：

- stdin：重定向到 `<dataPath>/<index>.in`
- stdout：写入 `<runtimeRoot>/<index>.out`
- stderr：写入 `<runtimeRoot>/<index>.err`
- 时间/内存：由 `ProcessUtils`（父进程 wall time kill + rlimit）和 `CgroupManager`（可用时）共同限制。

### 3.4 判题阶段（SandboxRuntime）

- 读取：
  - `StandardInput`：`<index>.in`
  - `StandardOutput`：`<index>.out`
  - `PersonalOutput`：运行产生的 `<runtimeRoot>/<index>.out`
- 判定优先级：
  1. 超时：`timedOut` 或 `SIGKILL/SIGXCPU` => `TLE`
  2. 超内存：`peakMemoryBytes > limit` 或 stderr 命中 OOM 关键字 => `MLE`
  3. 非 0 退出码或信号 => `RE`
  4. 正常退出：
     - 有 SPJ：以 SPJ 退出码为准（0=AC，否则=WA；SPJ 自身异常=SE）
     - 无 SPJ：去掉末尾空白后比较输出，不相等为 `WA`，相等为 `AC`

汇总规则：

- `RunTime/RunMemory`：取所有测试点最大值。
- `Status`：取第一个非 AC 的测试点状态（从 AC 变为 WA/RE/TLE/MLE），更贴近“快速失败”语义。

### 3.5 清理阶段

- 运行目录：默认在 `SandboxRuntime` 结束后删除 `<runtimeRoot>`，可通过 `XDOJ_KEEP_RUN_DIR=1` 保留。

## 4. 安全隔离与限制设计

### 4.1 Namespace（NamespaceManager）

入口：`NamespaceManager::apply(std::string& warning)`

- 尝试 `unshare(CLONE_NEWUSER | CLONE_NEWNS | CLONE_NEWIPC | CLONE_NEWUTS | CLONE_NEWNET)`
- 尝试写 `setgroups/uid_map/gid_map` 完成映射
- 设置 mount propagation 为 private

兼容模式：

- 部分宿主（例如容器内）可能禁止写 `uid_map`，导致用户命名空间不可用。
- 默认 `XDOJ_NAMESPACE_STRICT` 未开启时，Namespace 失败会降级为“跳过 Namespace”以保证判题功能可用。
- 线上如需强安全，请设置 `XDOJ_NAMESPACE_STRICT=1`，此时 Namespace 失败将直接拒绝执行并返回 `SE/RE`（由运行阶段捕获）。

### 4.2 Cgroups（CgroupManager）

入口：`CgroupManager::apply(int timeLimitMs, int64_t memoryLimitBytes, std::string& error)`

- 需要配置 `XDOJ_CGROUP_ROOT` 指向可写的 cgroup v2 子树。
- 写入：
  - `memory.max`
  - `pids.max`
  - `cpu.max`（固定单核配额）
  - `cgroup.procs`（将当前进程加入 cgroup）

严格模式：

- `XDOJ_CGROUP_STRICT=1` 时，如果 cgroup 创建/写入失败会拒绝运行。

清理：

- `ProcessUtils` 在子进程结束后尝试清理 `XDOJ_CGROUP_ROOT/judge-<pid>`，并优先写 `cgroup.kill`，避免遗留子进程。

### 4.3 seccomp（SeccompManager）

入口：`SeccompManager::apply(std::string& error)`

- 先设置 `no_new_privs`
- 加载一个“默认允许 + 黑名单拒绝”的过滤器，禁止高危 syscall（如 `ptrace/mount/unshare/reboot` 等）

说明：

- 生产级更理想的方式是白名单模型，但需要按语言运行时细化 syscall 集合；当前实现优先保证可用性并禁止明显高危操作。

## 5. 并发与稳定性设计

### 5.1 关键问题修复：Queue 并发错配

之前的实现中，`TaskManager::processTask` 对共享的 `CompileQueue/RunQueue` 做了 `push` 后立刻 `pop` 的同步调用。在多线程并发调用 `submitTask` 时，线程 A 可能 `pop` 到线程 B 的任务，造成提交结果串号，这是严重 correctness bug。

修复策略：

- `processTask` 改为对当前 `SandboxPreparedTask` 直接编译与运行。
- 并发容量控制仍由 `CompilerPool` 与 `SandboxPool/SandboxWorkerPool` 提供，不依赖 Queue 进行跨线程调度。

### 5.2 运行并发

- `SandboxWorkerPool` 的并发上限默认设置为 `SandboxPool::capacity()`，在大量请求下可充分利用槽位并限制资源占用。

## 6. 测试策略（sandbox_test）

`src/CodeSandbox/SandboxTestMain.cpp` 覆盖：

- `WA`：两测试点，第二点输出错误，校验 `Status=3` 且 `ComplierInfo==""`，并校验每个 `TestInfo` 条目字段存在且内容正确。
- `CE`：构造编译错误，校验 `Status=1` 且 `ComplierInfo` 非空。
- `RE`：构造段错误，校验 `Status=4`。
- `TLE`：死循环，校验 `Status=5`。
- `SPJ`：构造 `spj.cpp` 永远返回 0，校验即使输出错误也判 `AC`。

运行：

- `cmake --build build --target sandbox_test -j`
- `./build/sandbox_test`

