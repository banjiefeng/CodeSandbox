# CodeSandbox 测试用例设计文档

本文档用于对 `SandboxService::GetInstance()->submitTask(runjson)` 的判题结果进行系统测试，覆盖以下状态：

- `CE=1` 编译错误
- `AC=2` 通过
- `WA=3` 答案错误
- `RE=4` 运行时错误
- `TLE=5` 超时
- `MLE=6` 超内存
- `SE=7` 系统错误

测试语言：

- `C`
- `C++`
- `Java`
- `Python3`

## 1. 测试目标

验证 `CodeSandbox` 在四种语言下，能够根据用户代码、题目数据、资源限制和沙箱环境，稳定返回正确状态码，并且返回 JSON 结构满足既定协议。

## 2. 返回协议检查项

每条测试都应检查：

- 顶层字段存在：
  - `SubmitId`
  - `Status`
  - `ComplierInfo`
  - `RunTime`
  - `RunMemory`
  - `Length`
  - `TestInfo`
- `RunTime` 格式为 `*MS`
- `RunMemory` 格式为 `*MB`
- `Length` 格式为 `*B`
- `TestInfo[i]` 中字段存在：
  - `Status`
  - `RunTime`
  - `RunMemory`
  - `StandardInput`
  - `StandardOutput`
  - `PersonalOutput`

## 3. 通用测试数据

建议准备两个基础题目目录。

### 3.1 普通判题题目

`ProblemId = 2001`

测试数据：

- `1.in`
```text
1 2
```

- `1.out`
```text
3
```

- `2.in`
```text
5 6
```

- `2.out`
```text
11
```

用途：

- `AC`
- `WA`
- `RE`
- `TLE`
- `MLE`

### 3.2 SPJ/系统异常辅助题目

`ProblemId = 2002`

测试数据：

- `1.in`
```text
7 8
```

- `1.out`
```text
15
```

可选：

- `spj.cpp`

用途：

- 如需扩展 SPJ 测试可复用。
- 本文 `SE` 不依赖 SPJ，本题目可选。

## 4. 通用 runjson 模板

```json
{
  "SubmitId": "test_xxx",
  "ProblemId": "2001",
  "JudgeNum": 2,
  "Code": "用户代码",
  "Language": "C++",
  "TimeLimit": 1000,
  "MemoryLimit": 128
}
```

说明：

- `TimeLimit` 单位毫秒。
- `MemoryLimit` 单位 MB。
- `JudgeNum=2` 时要求存在 `1.in/1.out/2.in/2.out`。
- `MLE` 建议把 `MemoryLimit` 调低到 `32` 或 `64`，提高稳定性。
- `TLE` 建议把 `TimeLimit` 调低到 `50` 或 `100`，提高稳定性。

## 5. 系统错误（SE）测试方式

`SE=7` 不适合通过“用户程序本身”制造，因为它表示的是沙箱系统级失败。推荐统一采用下面两种方式之一：

### 方式 A：Namespace 严格模式

设置环境变量：

```bash
export XDOJ_NAMESPACE_STRICT=1
```

然后提交一份本应 `AC` 的正常程序。

预期：

- 如果当前宿主机不允许无特权用户命名空间或 `uid_map/gid_map` 写入失败，则返回 `SE=7`。

### 方式 B：Cgroup 严格模式

设置环境变量：

```bash
export XDOJ_CGROUP_STRICT=1
export XDOJ_CGROUP_ROOT=/path/not/exist
```

然后提交一份本应 `AC` 的正常程序。

预期：

- 创建或写入 cgroup 失败，返回 `SE=7`。

建议：

- 自动化测试优先使用方式 B，更可控。
- 由于 `SE` 是环境触发，所以四种语言都可以复用同一个“正常 AC 程序”，只改变 `Language/Code`。

## 6. C 语言测试用例

### C-CE-01 编译错误

目标状态：

- `CE=1`

推荐 `runjson`：

- `ProblemId=2001`
- `JudgeNum=1`
- `Language="C"`

代码：

```c
#include <stdio.h>
int main() {
    printf("hello")
    return 0;
}
```

预期：

- 顶层 `Status=1`
- `ComplierInfo` 非空

### C-AC-01 正确答案

目标状态：

- `AC=2`

代码：

```c
#include <stdio.h>
int main() {
    long long a, b;
    if (scanf("%lld%lld", &a, &b) != 2) return 0;
    printf("%lld\n", a + b);
    return 0;
}
```

预期：

- 顶层 `Status=2`
- 两个测试点都为 `2`

### C-WA-01 答案错误

目标状态：

- `WA=3`

代码：

```c
#include <stdio.h>
int main() {
    long long a, b;
    if (scanf("%lld%lld", &a, &b) != 2) return 0;
    if (a == 5 && b == 6) {
        printf("10\n");
    } else {
        printf("%lld\n", a + b);
    }
    return 0;
}
```

预期：

- 顶层 `Status=3`
- 测试点 1 为 `AC`
- 测试点 2 为 `WA`

### C-RE-01 运行时错误

目标状态：

- `RE=4`

代码：

```c
int main() {
    int *p = 0;
    *p = 1;
    return 0;
}
```

预期：

- `Status=4`

### C-TLE-01 超时

目标状态：

- `TLE=5`

推荐：

- `TimeLimit=50`

代码：

```c
int main() {
    while (1) {}
    return 0;
}
```

预期：

- `Status=5`

### C-MLE-01 超内存

目标状态：

- `MLE=6`

推荐：

- `MemoryLimit=32`

代码：

```c
#include <stdlib.h>
#include <string.h>
int main() {
    while (1) {
        void *p = malloc(8 * 1024 * 1024);
        if (!p) {
            memset((void *)0, 0, 1);
        }
        memset(p, 1, 8 * 1024 * 1024);
    }
    return 0;
}
```

预期：

- `Status=6`

说明：

- 这里故意持续申请内存并触碰页面。
- 如果 `malloc` 失败后触发空指针写，沙箱应优先根据内存限制识别为 `MLE`，而不是 `RE`。

### C-SE-01 系统错误

目标状态：

- `SE=7`

环境：

```bash
export XDOJ_CGROUP_STRICT=1
export XDOJ_CGROUP_ROOT=/path/not/exist
```

代码：

使用 `C-AC-01` 的正常代码。

预期：

- `Status=7`

## 7. C++ 测试用例

### CPP-CE-01 编译错误

目标状态：

- `CE=1`

代码：

```cpp
#include <bits/stdc++.h>
int main() {
    std::cout << "hello" << std::endl
    return 0;
}
```

预期：

- `Status=1`
- `ComplierInfo` 非空

### CPP-AC-01 正确答案

目标状态：

- `AC=2`

代码：

```cpp
#include <bits/stdc++.h>
int main() {
    long long a, b;
    if (!(std::cin >> a >> b)) return 0;
    std::cout << a + b << "\n";
}
```

预期：

- `Status=2`

### CPP-WA-01 答案错误

目标状态：

- `WA=3`

代码：

```cpp
#include <bits/stdc++.h>
int main() {
    long long a, b;
    if (!(std::cin >> a >> b)) return 0;
    if (a == 5 && b == 6) std::cout << 10 << "\n";
    else std::cout << a + b << "\n";
}
```

预期：

- `Status=3`

### CPP-RE-01 运行时错误

目标状态：

- `RE=4`

代码：

```cpp
#include <bits/stdc++.h>
int main() {
    int *p = nullptr;
    *p = 1;
}
```

预期：

- `Status=4`

### CPP-TLE-01 超时

目标状态：

- `TLE=5`

推荐：

- `TimeLimit=50`

代码：

```cpp
#include <bits/stdc++.h>
int main() {
    while (true) {}
}
```

预期：

- `Status=5`

### CPP-MLE-01 超内存

目标状态：

- `MLE=6`

推荐：

- `MemoryLimit=32`

代码：

```cpp
#include <bits/stdc++.h>
int main() {
    std::vector<std::string> v;
    while (true) {
        v.emplace_back(8 * 1024 * 1024, 'a');
    }
}
```

预期：

- `Status=6`

### CPP-SE-01 系统错误

目标状态：

- `SE=7`

环境：

```bash
export XDOJ_CGROUP_STRICT=1
export XDOJ_CGROUP_ROOT=/path/not/exist
```

代码：

使用 `CPP-AC-01` 的正常代码。

预期：

- `Status=7`

## 8. Java 测试用例

Java 推荐统一使用：

- `Language="Java"`
- 源文件类名必须为 `Main`

### JAVA-CE-01 编译错误

目标状态：

- `CE=1`

代码：

```java
public class Main {
    public static void main(String[] args) {
        System.out.println("hello")
    }
}
```

预期：

- `Status=1`

### JAVA-AC-01 正确答案

目标状态：

- `AC=2`

代码：

```java
import java.util.*;
public class Main {
    public static void main(String[] args) {
        Scanner in = new Scanner(System.in);
        if (!in.hasNextLong()) return;
        long a = in.nextLong();
        long b = in.nextLong();
        System.out.println(a + b);
    }
}
```

预期：

- `Status=2`

### JAVA-WA-01 答案错误

目标状态：

- `WA=3`

代码：

```java
import java.util.*;
public class Main {
    public static void main(String[] args) {
        Scanner in = new Scanner(System.in);
        if (!in.hasNextLong()) return;
        long a = in.nextLong();
        long b = in.nextLong();
        if (a == 5 && b == 6) System.out.println(10);
        else System.out.println(a + b);
    }
}
```

预期：

- `Status=3`

### JAVA-RE-01 运行时错误

目标状态：

- `RE=4`

代码：

```java
public class Main {
    public static void main(String[] args) {
        int x = 1 / 0;
        System.out.println(x);
    }
}
```

预期：

- `Status=4`

### JAVA-TLE-01 超时

目标状态：

- `TLE=5`

推荐：

- `TimeLimit=100`

代码：

```java
public class Main {
    public static void main(String[] args) {
        while (true) {}
    }
}
```

预期：

- `Status=5`

### JAVA-MLE-01 超内存

目标状态：

- `MLE=6`

推荐：

- `MemoryLimit=64`

代码：

```java
import java.util.*;
public class Main {
    public static void main(String[] args) {
        List<byte[]> list = new ArrayList<>();
        while (true) {
            list.add(new byte[8 * 1024 * 1024]);
        }
    }
}
```

预期：

- `Status=6`

说明：

- 通常会触发 `OutOfMemoryError`，stderr 含关键字，可判为 `MLE`。

### JAVA-SE-01 系统错误

目标状态：

- `SE=7`

环境：

```bash
export XDOJ_CGROUP_STRICT=1
export XDOJ_CGROUP_ROOT=/path/not/exist
```

代码：

使用 `JAVA-AC-01` 的正常代码。

预期：

- `Status=7`

## 9. Python3 测试用例

## 9.1 重要说明

当前 `CodeSandbox` 实现中，`PythonRunner::requiresCompilation()` 返回 `false`，即 Python3 默认没有显式编译阶段。

这意味着：

- 如果不改现有实现，Python3 的语法错误通常会在运行时由解释器报错，更接近 `RE=4`，而不是 `CE=1`。

因此本文给出两套方案：

- `推荐方案`：给 Python3 增加预编译检查 `python3 -m py_compile main.py`，然后语法错误测试预期为 `CE=1`
- `当前实现兼容方案`：不修改代码时，语法错误测试预期为 `RE=4`

如果你的目标是“四种语言都完整覆盖七种状态”，建议先补 Python3 编译前语法检查。

### PY-CE-01 编译错误（推荐方案）

目标状态：

- `CE=1`

前提：

- Python3 runner 增加预编译阶段，例如：
```bash
python3 -m py_compile main.py
```

代码：

```python
def main()
    print("hello")

if __name__ == "__main__":
    main()
```

预期：

- `Status=1`
- `ComplierInfo` 含 `SyntaxError`

### PY-CE-01A 当前实现兼容说明

如果不增加 Python3 编译阶段，上述用例在当前实现下更可能返回：

- `RE=4`

### PY-AC-01 正确答案

目标状态：

- `AC=2`

代码：

```python
import sys

def main():
    data = sys.stdin.read().strip().split()
    if len(data) < 2:
        return
    a, b = map(int, data[:2])
    print(a + b)

if __name__ == "__main__":
    main()
```

预期：

- `Status=2`

### PY-WA-01 答案错误

目标状态：

- `WA=3`

代码：

```python
import sys

def main():
    data = sys.stdin.read().strip().split()
    if len(data) < 2:
        return
    a, b = map(int, data[:2])
    if a == 5 and b == 6:
        print(10)
    else:
        print(a + b)

if __name__ == "__main__":
    main()
```

预期：

- `Status=3`

### PY-RE-01 运行时错误

目标状态：

- `RE=4`

代码：

```python
print(1 // 0)
```

预期：

- `Status=4`

### PY-TLE-01 超时

目标状态：

- `TLE=5`

推荐：

- `TimeLimit=100`

代码：

```python
while True:
    pass
```

预期：

- `Status=5`

### PY-MLE-01 超内存

目标状态：

- `MLE=6`

推荐：

- `MemoryLimit=64`

代码：

```python
arr = []
while True:
    arr.append(bytearray(8 * 1024 * 1024))
```

预期：

- `Status=6`

说明：

- 常见表现是抛出 `MemoryError`，stderr 含关键字，可被识别为 `MLE`。

### PY-SE-01 系统错误

目标状态：

- `SE=7`

环境：

```bash
export XDOJ_CGROUP_STRICT=1
export XDOJ_CGROUP_ROOT=/path/not/exist
```

代码：

使用 `PY-AC-01` 的正常代码。

预期：

- `Status=7`

## 10. 自动化执行建议

建议按下面顺序组织自动化测试：

1. 先跑四种语言的 `AC/WA/RE/TLE/MLE`
2. 再跑三种编译型语言 `CE`
3. Python3 的 `CE` 分开处理：
   - 如果已支持 `py_compile`，按 `CE=1` 验证
   - 如果未支持，记录为“当前实现不支持此状态”
4. 最后单独跑四种语言的 `SE`

原因：

- `SE` 依赖环境变量，容易污染其他用例，应单独执行。
- `TLE/MLE` 执行时间更长，建议放在中后段。

## 11. 结果判定表

| 语言 | CE | AC | WA | RE | TLE | MLE | SE |
| --- | --- | --- | --- | --- | --- | --- | --- |
| C | 支持 | 支持 | 支持 | 支持 | 支持 | 支持 | 支持 |
| C++ | 支持 | 支持 | 支持 | 支持 | 支持 | 支持 | 支持 |
| Java | 支持 | 支持 | 支持 | 支持 | 支持 | 支持 | 支持 |
| Python3 | 需要预编译检查 | 支持 | 支持 | 支持 | 支持 | 支持 | 支持 |

## 12. 建议补充

为了让 Python3 与其它语言一样完整覆盖 `CE=1`，建议在 `PythonRunner` 增加编译前语法检查，典型做法：

```bash
python3 -m py_compile main.py
```

这样 Python3 的：

- 语法错误 => `CE=1`
- 运行时异常 => `RE=4`

边界会更清晰，测试文档也能完全闭环。

