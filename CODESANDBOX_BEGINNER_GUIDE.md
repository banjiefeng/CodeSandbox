# CodeSandbox 新手学习文档

本文档面向第一次接触判题沙箱的新同学，按模块解释 `src/CodeSandbox/` 的设计与实现。阅读顺序建议从“入口服务”一路跟到“进程执行与隔离”，最后看测试。

代码入口：

`SandboxService::GetInstance()->submitTask(runjson)`

## 1. 你要先理解的三件事

1. 判题不是交互式输入：用户程序的标准输入来自题目数据目录里的 `*.in` 文件。
2. 判题分两段：先编译（可能失败 => CE），再逐测试点运行（产生 AC/WA/RE/TLE/MLE/SE）。
3. 安全靠三层：namespace 做隔离、cgroups/rlimit 做资源限制、seccomp 限制危险系统调用。

## 2. 模块总览

目录结构（关键）：

- `service/`：对外服务入口（解析 JSON，组装任务，调用 TaskManager）
- `scheduler/`：核心编排（TaskManager + 结果收集）
- `workerPools/`：并发控制与资源复用（CompilerPool、SandboxPool、SandboxWorkerPool）
- `languageRunners/`：不同语言的编译/运行命令构造（策略模式）
- `sandboxRuntime/`：真正执行用户程序与隔离（Namespace/Cgroup/Seccomp + Filesystem）
- `ProcessUtils.*`：fork/exec + I/O 重定向 + 超时监控 + rusage 采样
- `SandboxTypes.hpp`：任务/结果结构与 JSON 协议
- `SandboxTestMain.cpp`：端到端自动化测试

## 3. service 模块：SandboxService

文件：

- [SandboxService.hpp](/home/jinzheyu/sandboxSet/sandbox.0.2/XDOJ/src/CodeSandbox/service/SandboxService.hpp)
- [SandboxService.cpp](/home/jinzheyu/sandboxSet/sandbox.0.2/XDOJ/src/CodeSandbox/service/SandboxService.cpp)

职责：

- 把外部 `runjson` 转成内部 `SandboxTask`（类型更稳定，不直接在各处读 JSON）
- 注册语言运行器（C/C++/Go/Java/Python3）
- 持有 `TaskManager`，并把任务交给它执行
- 计算题目数据目录 `dataPath`

新手易错点：

- `ProblemId/SubmitId` 需要能转成字符串（代码里支持 int/string 两种）
- `MemoryLimit` 输入是 MB，但内部统一转成 bytes

## 4. scheduler 模块：TaskManager + ResultCollector

文件：

- [TaskManager.hpp](/home/jinzheyu/sandboxSet/sandbox.0.2/XDOJ/src/CodeSandbox/scheduler/TaskManager.hpp)
- [TaskManager.cpp](/home/jinzheyu/sandboxSet/sandbox.0.2/XDOJ/src/CodeSandbox/scheduler/TaskManager.cpp)
- [ResultCollector.hpp](/home/jinzheyu/sandboxSet/sandbox.0.2/XDOJ/src/CodeSandbox/scheduler/ResultCollector.hpp)
- [ResultCollector.cpp](/home/jinzheyu/sandboxSet/sandbox.0.2/XDOJ/src/CodeSandbox/scheduler/ResultCollector.cpp)

TaskManager 的职责（按顺序）：

1. 校验任务参数（避免无意义的编译/运行）
2. 查找对应 `LanguageRunner`
3. 装载测试点清单（必须存在 `1..JudgeNum` 的 `.in/.out`）
4. 处理 SPJ：如果题目目录存在 `spj.cpp`，则编译出 `spj` 可执行文件
5. 调用 `CompilerPool` 编译用户代码：
   - 失败 => 直接返回 `CE`
6. 调用 `SandboxWorkerPool` 运行（逐测试点）并得到 `SandboxRunResult`
7. 交给 `ResultCollector` 汇总为最终 JSON

ResultCollector 的职责：

- 把编译结果 + 运行结果合成 `SandboxFinalResult`
- 控制 `ComplierInfo` 的语义：
  - 只有 `CE` 和 `SE` 才会填充信息

新手易错点：

- 为什么不需要“任务队列”？因为当前实现是同步请求-响应模式，容量控制已经由 pool 完成。

## 5. workerPools：并发与复用

### 5.1 CompilerPool（编译并发）

文件：

- [CompilerPool.hpp](/home/jinzheyu/sandboxSet/sandbox.0.2/XDOJ/src/CodeSandbox/workerPools/CompilerPool.hpp)
- [CompilerPool.cpp](/home/jinzheyu/sandboxSet/sandbox.0.2/XDOJ/src/CodeSandbox/workerPools/CompilerPool.cpp)

职责：

- 限制并发编译数量（避免瞬间大量 `g++/javac/go` 抢光 CPU/内存）
- 准备编译目录 `/tmp/xdoj-sandbox/build/<SubmitId>/`
- 写源代码文件，执行编译命令，抓取 stdout/stderr

实现要点：

- `activeWorkers + condition_variable` 相当于一个“计数信号量”
- Java 编译（`javac`）在某些环境对虚拟地址空间非常敏感：编译阶段不对 `javac` 施加 RLIMIT_AS（运行阶段仍严格限制用户程序）

### 5.2 SandboxPool（沙箱槽位）

文件：

- [SandboxPool.hpp](/home/jinzheyu/sandboxSet/sandbox.0.2/XDOJ/src/CodeSandbox/workerPools/SandboxPool.hpp)
- [SandboxPool.cpp](/home/jinzheyu/sandboxSet/sandbox.0.2/XDOJ/src/CodeSandbox/workerPools/SandboxPool.cpp)

职责：

- 预创建固定数量槽位目录：`/tmp/xdoj-sandbox/pool/<slot>/`
- 高并发下通过 `acquire/release` 复用槽位（对象池模式）

为什么要对象池：

- 避免每次运行都创建很多层目录导致抖动
- 方便控制“同时允许多少个运行任务”

### 5.3 SandboxWorkerPool（运行并发）

文件：

- [SandboxWorkerPool.hpp](/home/jinzheyu/sandboxSet/sandbox.0.2/XDOJ/src/CodeSandbox/workerPools/SandboxWorkerPool.hpp)
- [SandboxWorkerPool.cpp](/home/jinzheyu/sandboxSet/sandbox.0.2/XDOJ/src/CodeSandbox/workerPools/SandboxWorkerPool.cpp)

职责：

- 控制“同时运行多少个提交”
- 为每个运行任务从 `SandboxPool` 获取一个槽位

## 6. languageRunners：策略模式

文件：

- [LanguageRunner.hpp](/home/jinzheyu/sandboxSet/sandbox.0.2/XDOJ/src/CodeSandbox/languageRunners/LanguageRunner.hpp)
- 各语言 runner（C/Cpp/Go/Java/Python）

职责：

- 把“语言差异”限定在少数方法里：
  - 源文件名（如 `main.cpp` / `Main.java`）
  - 可执行名
  - 编译命令
  - 运行命令
  - 进程/线程限制建议（Java 这里返回 0 表示不额外设置 RLIMIT_NPROC）

关键点：

- Python3 也有“编译阶段”：使用 `python3 -m py_compile` 做语法检查，让语法错误归类到 `CE`

## 7. sandboxRuntime：真正的沙箱执行

文件：

- [SandboxRuntime.hpp](/home/jinzheyu/sandboxSet/sandbox.0.2/XDOJ/src/CodeSandbox/sandboxRuntime/SandboxRuntime.hpp)
- [SandboxRuntime.cpp](/home/jinzheyu/sandboxSet/sandbox.0.2/XDOJ/src/CodeSandbox/sandboxRuntime/SandboxRuntime.cpp)
- [FilesystemManager.hpp](/home/jinzheyu/sandboxSet/sandbox.0.2/XDOJ/src/CodeSandbox/sandboxRuntime/FilesystemManager.hpp)
- [NamespaceManager.hpp](/home/jinzheyu/sandboxSet/sandbox.0.2/XDOJ/src/CodeSandbox/sandboxRuntime/NamespaceManager.hpp)
- [CgroupManager.hpp](/home/jinzheyu/sandboxSet/sandbox.0.2/XDOJ/src/CodeSandbox/sandboxRuntime/CgroupManager.hpp)
- [SeccompManager.hpp](/home/jinzheyu/sandboxSet/sandbox.0.2/XDOJ/src/CodeSandbox/sandboxRuntime/SeccompManager.hpp)

### 7.1 FilesystemManager

职责：

- 为某个槽位 + 提交创建运行目录（`.../sandbox/<SubmitId>/work`）
- 把编译产物复制到运行目录

### 7.2 NamespaceManager

职责：

- 尽力创建 `user/ns/mount/net/...` 命名空间隔离

严格模式：

- `XDOJ_NAMESPACE_STRICT=1`：必须成功，否则拒绝运行（系统级失败会被标为 `SE`）

### 7.3 CgroupManager

职责：

- 如果配置了 `XDOJ_CGROUP_ROOT`，则写 cgroup v2 限制（memory/pids/cpu）
- `XDOJ_CGROUP_STRICT=1`：要求 cgroup 必须成功，否则返回系统错误

### 7.4 SeccompManager

职责：

- 禁止一些高危 syscall（黑名单模型）

### 7.5 SandboxRuntime 判题逻辑

核心循环（每个测试点）：

1. 设置 stdin/stdout/stderr 重定向路径
2. fork/exec 运行用户程序，并在 `beforeExec` 里执行 namespace/cgroup/seccomp
3. 根据结果判定状态：
   - 超时 => `TLE`
   - 超内存（含 stderr 关键字）=> `MLE`
   - 非 0 退出/信号 => `RE`
   - 输出不一致 => `WA`（无 SPJ 时）
   - 通过 => `AC`
4. 汇总取最大运行时间/内存，取第一个非 AC 状态作为总状态

系统错误（SE）是怎么来的：

- `ProcessUtils` 给内部失败定义了专用 exit code（例如 beforeExec/exec 失败），运行阶段识别后映射为 `SE`

## 8. ProcessUtils：fork/exec 的统一封装

文件：

- [ProcessUtils.hpp](/home/jinzheyu/sandboxSet/sandbox.0.2/XDOJ/src/CodeSandbox/ProcessUtils.hpp)
- [ProcessUtils.cpp](/home/jinzheyu/sandboxSet/sandbox.0.2/XDOJ/src/CodeSandbox/ProcessUtils.cpp)

职责：

- `fork` 子进程
- `dup2` 重定向 stdin/stdout/stderr
- `setrlimit` 设置 RLIMIT_AS / RLIMIT_CPU / RLIMIT_NPROC（可选）
- 调用 `beforeExec`（沙箱隔离注入点）
- `execvp` 执行
- 父进程轮询 `wait4(WNOHANG)`，超时则 `SIGKILL`
- 采集 `rusage` 得到 `ru_maxrss` 作为峰值内存近似
- 清理 cgroup 目录（如果配置了 `XDOJ_CGROUP_ROOT`）

## 9. SandboxTypes：任务与 JSON 协议

文件：

- [SandboxTypes.hpp](/home/jinzheyu/sandboxSet/sandbox.0.2/XDOJ/src/CodeSandbox/SandboxTypes.hpp)

你需要记住的协议要点：

- `TestInfo[i].StandardInput` 必须始终存在（即使为空）
- `ComplierInfo` 只给 `CE` 和 `SE` 用

## 10. 自动化测试：SandboxTestMain

文件：

- [SandboxTestMain.cpp](/home/jinzheyu/sandboxSet/sandbox.0.2/XDOJ/src/CodeSandbox/SandboxTestMain.cpp)
- 设计文档：[CODESANDBOX_TESTPLAN.md](/home/jinzheyu/sandboxSet/sandbox.0.2/XDOJ/src/CodeSandbox/CODESANDBOX_TESTPLAN.md)

这份测试会覆盖：

- C/C++/Java/Python3 四种语言
- 每种语言 `CE/AC/WA/RE/TLE/MLE/SE`

调试开关：

- `XDOJ_TEST_FILTER=JAVA-AC`：只跑匹配的用例名
- `XDOJ_TEST_DUMP=1`：打印该用例的返回 JSON

## 11. 后续优化方案（建议路线）

### 11.1 添加“正确的任务队列”（重点）

现状：

- `submitTask` 是同步阻塞，靠 pool 做并发限流。

要支持“海量请求排队”并保持吞吐，推荐的队列设计：

1. 引入一个真正的 `TaskQueue`（生产者-消费者）：
   - 生产者：HTTP/业务线程调用 `submitTask` 时只负责入队
   - 消费者：固定数量 worker 线程从队列取任务执行编译+运行
2. 每个任务携带一个 `promise/future`（或回调）：
   - `submitTask` 可以选择阻塞等待 future（保持现有同步接口）
   - 或提供异步接口 `submitTaskAsync` 返回 taskId
3. 队列需要“任务隔离”：
   - 不能用“共享队列 push 立刻 pop”这种写法
   - 必须保证取出的一定是自己要执行的任务对象
4. 增加 backpressure：
   - 队列长度上限
   - 超过上限返回 `SE` 或特定错误码（看你业务定义）

### 11.2 提升隔离强度

- namespace：在支持的宿主机启用 userns + mount namespace 后，可以进一步做：
  - `chroot/pivot_root` 到最小 rootfs（目前 rootfs 目录已预留但未构建最小系统）
  - 只挂载必要的只读目录（`/lib` `/usr/lib` 等）
- seccomp：从黑名单逐步收敛到“按语言白名单”

### 11.3 更精确的资源统计

- 现在用 `ru_maxrss` 近似内存峰值，对多进程/多线程语言可能不够准确
- 可扩展为读取 cgroup v2 的 `memory.peak`（如果启用 cgroup）

### 11.4 输出比较与判题增强

- 当前默认比较是“去掉末尾空白”，可扩展为：
  - 忽略行尾空格
  - 浮点误差比较
  - 自定义 checker（SPJ 已支持）

