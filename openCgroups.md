 XDOJ_CGROUP_ROOT 是一个环境变量：在启动沙箱进程前把它设置成“可写的 cgroup v2
  子树目录”，沙箱就会在该目录下创建 judge-<pid> 并写入
  memory.max/pids.max/cpu.max/cgroup.procs 来做限制（见 CgroupManager.cpp）。

  最常见的开启方式（宿主机是 cgroup v2，且你有权限写 cgroup；通常需要 root 或
  systemd 委派）：

  # 1) 确认是 cgroup v2
  test -f /sys/fs/cgroup/cgroup.controllers

  # 2) 准备一个子树（必须在 cgroupfs 里，比如 /sys/fs/cgroup 下）
  sudo mkdir -p /sys/fs/cgroup/xdoj
  # 让子树可用 cpu/memory/pids 控制器（写入位置可能需要按你的实际父 cgroup 调
  整）
  echo "+cpu +memory +pids" | sudo tee /sys/fs/cgroup/cgroup.subtree_control >/
  dev/null

  # 3) 运行前开启
  export XDOJ_CGROUP_ROOT=/sys/fs/cgroup/xdoj
  # 可选：强制 cgroup 必须成功，否则拒绝运行
  export XDOJ_CGROUP_STRICT=1

  # 4) 启动你的 codesandbox / sandbox_test

  如果你设置了 XDOJ_CGROUP_ROOT 但因为权限/控制器未启用导致创建或写入失败：不开
  XDOJ_CGROUP_STRICT=1 时会静默降级（仍会走 rlimit）；开了严格模式会直接报错拒绝
  运行（见 CODESANDBOX_DESIGN.md）。

## 注：Cgroups-v2与Cgroups-v1区别
**理解 Linux cgroup 的关键概念**。

1️⃣ 什么是控制器
2️⃣ 什么是控制器分散（cgroup v1）
3️⃣ 什么是统一层级（cgroup v2）
4️⃣ 什么是虚拟文件系统
5️⃣ cgroup 如何通过虚拟文件系统控制资源

---

# 一、什么是控制器（Controller）

**控制器就是一种资源限制模块。**

每个控制器负责一种资源。

常见控制器：

| 控制器    | 作用      |
| ------ | ------- |
| cpu    | 限制CPU使用 |
| memory | 限制内存    |
| pids   | 限制进程数量  |
| io     | 限制磁盘IO  |

例如：

```text
memory 控制器 → 控制内存
cpu 控制器 → 控制CPU
pids 控制器 → 控制进程数量
```

如果一个程序加入某个 cgroup，就会被这些控制器限制。

---

# 二、什么是“控制器分散”（cgroup v1）

在 **cgroup v1** 中：

**每个控制器都有自己的目录树。**

例如系统结构：

```text
/sys/fs/cgroup
 ├── cpu
 │    ├── groupA
 │    └── groupB
 │
 ├── memory
 │    ├── groupA
 │    └── groupB
 │
 └── pids
      ├── groupA
      └── groupB
```

这里出现一个问题：

同一个进程如果要限制：

* CPU
* 内存
* 进程数量

就必须 **分别加入三个不同的目录**。

例如：

```bash
echo 1234 > /sys/fs/cgroup/cpu/groupA/cgroup.procs
echo 1234 > /sys/fs/cgroup/memory/groupA/cgroup.procs
echo 1234 > /sys/fs/cgroup/pids/groupA/cgroup.procs
```

问题：

❌ 管理复杂
❌ 结构重复
❌ 可能不同步

这就叫：

**控制器分散（Controllers are scattered）**

---

# 三、什么是“统一层级”（cgroup v2）

在 **cgroup v2** 中：

**所有控制器共享同一棵树。**

结构：

```text
/sys/fs/cgroup
 └── xdoj
      ├── judge-1001
      └── judge-1002
```

在同一个目录里：

```text
memory.max
cpu.max
pids.max
```

例如：

```text
/sys/fs/cgroup/xdoj/judge-1001
 ├── cpu.max
 ├── memory.max
 ├── pids.max
 └── cgroup.procs
```

一个进程只需要加入一次：

```bash
echo 1234 > cgroup.procs
```

就同时受到：

* CPU限制
* 内存限制
* 进程限制

这就叫：

**统一层级（Unified hierarchy）**

---

# 四、什么是虚拟文件系统（Virtual File System）

Linux 有很多 **不是磁盘上的文件系统**。

例如：

| 目录             | 用途     |
| -------------- | ------ |
| /proc          | 进程信息   |
| /sys           | 设备信息   |
| /sys/fs/cgroup | cgroup |

这些文件：

* 不在磁盘
* 是 **内核动态生成**

例如：

```bash
cat /proc/cpuinfo
```

你看到的是：

```text
cpu model
cpu cores
cache size
```

但实际上：

❌ 不是磁盘文件
✅ 是 **内核实时生成的数据**

这类系统叫：

**虚拟文件系统（VFS）**

---

# 五、cgroup 如何通过虚拟文件系统控制资源

cgroup 的设计思想非常巧妙：

**通过读写文件控制内核行为。**

例如：

### 限制内存

写入：

```bash
echo 256M > memory.max
```

内核收到指令：

```text
这个 cgroup 的内存上限 = 256MB
```

当程序超过：

```text
256MB
```

内核会：

```text
OOM Kill
```

杀死进程。

---

### 限制进程数

写入：

```bash
echo 10 > pids.max
```

表示：

```text
最多10个进程
```

如果程序执行：

```c
fork();
```

超过10个：

```text
fork() 失败
```

---

### 限制CPU

写入：

```bash
echo "50000 100000" > cpu.max
```

表示：

```text
50% CPU
```

---

# 六、进程如何加入 cgroup

加入方式：

```bash
echo PID > cgroup.procs
```

例如：

```bash
echo 1234 > /sys/fs/cgroup/xdoj/judge-1234/cgroup.procs
```

之后：

```text
进程1234受到该目录所有限制
```

---

# 七、为什么 Linux 用“文件”控制资源

这是 Linux 的设计哲学：

**Everything is a file**

优点：

1️⃣ API非常简单

```text
read()
write()
```

2️⃣ shell可以直接操作

```bash
echo
cat
```

3️⃣ 编程简单

C++ 示例：

```cpp
std::ofstream f("/sys/fs/cgroup/xdoj/judge/memory.max");
f << "256M";
```

---

# 八、OJ 沙箱如何使用它

OJ 沙箱流程：

```
用户代码提交
      │
HTTP服务器
      │
Sandbox创建cgroup
      │
限制资源
      │
fork子进程
      │
加入cgroup
      │
exec用户程序
```

目录结构：

```
/sys/fs/cgroup/xdoj
 └── judge-4321
      ├── cpu.max
      ├── memory.max
      ├── pids.max
      └── cgroup.procs
```

---

# 九、一句话总结三个概念

**控制器**

```
资源管理模块
(cpu memory pids)
```

**控制器分散（v1）**

```
每个控制器一棵树
```

**统一层级（v2）**

```
所有控制器一棵树
```

---

# 十、最直观对比

### cgroup v1

```
cpu/
memory/
pids/
```

进程需要加入 **多个地方**。

---

### cgroup v2

```
一个目录
 ├ cpu.max
 ├ memory.max
 └ pids.max
```

进程 **加入一次即可**。

---

