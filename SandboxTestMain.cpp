#include "./service/SandboxService.hpp"

#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <string>
#include <vector>

namespace fs = std::filesystem;

namespace {

struct LanguagePrograms
{
    std::string tag;
    std::string language;
    std::string ce;
    std::string ac;
    std::string wa;
    std::string re;
    std::string tle;
    std::string mle;
};

struct EnvGuard
{
    explicit EnvGuard(const std::string& key) : key(key)
    {
        const char* value = std::getenv(key.c_str());
        if (value != nullptr)
        {
            existed = true;
            oldValue = value;
        }
    }

    ~EnvGuard()
    {
        if (existed)
        {
            setenv(key.c_str(), oldValue.c_str(), 1);
        }
        else
        {
            unsetenv(key.c_str());
        }
    }

    std::string key;
    std::string oldValue;
    bool existed = false;
};

void writeTextFile(const fs::path& path, const std::string& content)
{
    fs::create_directories(path.parent_path());
    std::ofstream out(path.string().c_str(), std::ios::out | std::ios::binary);
    out << content;
}

bool endsWith(const std::string& value, const std::string& suffix)
{
    return value.size() >= suffix.size() &&
           value.compare(value.size() - suffix.size(), suffix.size(), suffix) == 0;
}

void require(bool ok, const std::string& message)
{
    if (!ok)
    {
        std::cerr << "TEST FAILED: " << message << std::endl;
        std::exit(2);
    }
}

bool shouldRunCase(const std::string& caseName)
{
    const char* filter = std::getenv("XDOJ_TEST_FILTER");
    if (filter == nullptr || *filter == '\0')
    {
        return true;
    }
    return caseName.find(filter) != std::string::npos;
}

bool shouldDumpResult()
{
    const char* dump = std::getenv("XDOJ_TEST_DUMP");
    return dump != nullptr && std::string(dump) == "1";
}

void dumpResultIfNeeded(const std::string& caseName, const Json::Value& result)
{
    if (!shouldDumpResult())
    {
        return;
    }
    std::cerr << "==== " << caseName << " RESULT ====" << std::endl;
    std::cerr << result.toStyledString() << std::endl;
}

void requireHasField(const Json::Value& value, const std::string& key)
{
    require(value.isMember(key), "missing field: " + key);
}

void validateTopLevel(const Json::Value& result)
{
    require(result.isObject(), "result must be object");
    requireHasField(result, "SubmitId");
    requireHasField(result, "Status");
    requireHasField(result, "ComplierInfo");
    requireHasField(result, "RunTime");
    requireHasField(result, "RunMemory");
    requireHasField(result, "Length");
    requireHasField(result, "TestInfo");

    require(result["SubmitId"].isString(), "SubmitId must be string");
    require(result["Status"].isInt(), "Status must be int");
    require(result["ComplierInfo"].isString(), "ComplierInfo must be string");
    require(result["RunTime"].isString() && endsWith(result["RunTime"].asString(), "MS"), "RunTime format");
    require(result["RunMemory"].isString() && endsWith(result["RunMemory"].asString(), "MB"), "RunMemory format");
    require(result["Length"].isString() && endsWith(result["Length"].asString(), "B"), "Length format");
    require(result["TestInfo"].isArray(), "TestInfo must be array");
}

void validateTestItem(const Json::Value& item)
{
    require(item.isObject(), "TestInfo item must be object");
    requireHasField(item, "Status");
    requireHasField(item, "RunTime");
    requireHasField(item, "RunMemory");
    requireHasField(item, "StandardInput");
    requireHasField(item, "StandardOutput");
    requireHasField(item, "PersonalOutput");

    require(item["Status"].isInt(), "item.Status must be int");
    require(item["RunTime"].isString() && endsWith(item["RunTime"].asString(), "MS"), "item.RunTime format");
    require(item["RunMemory"].isString() && endsWith(item["RunMemory"].asString(), "MB"), "item.RunMemory format");
    require(item["StandardInput"].isString(), "item.StandardInput must be string");
    require(item["StandardOutput"].isString(), "item.StandardOutput must be string");
    require(item["PersonalOutput"].isString(), "item.PersonalOutput must be string");
}

Json::Value buildTask(const std::string& submitId,
                      const std::string& problemId,
                      const std::string& language,
                      const std::string& code,
                      int judgeNum,
                      int timeLimit,
                      int memoryLimit)
{
    Json::Value task;
    task["SubmitId"] = submitId;
    task["ProblemId"] = problemId;
    task["Language"] = language;
    task["JudgeNum"] = judgeNum;
    task["TimeLimit"] = timeLimit;
    task["MemoryLimit"] = memoryLimit;
    task["Code"] = code;
    return task;
}

void prepareProblemData(SandboxService* service, const std::string& problemId)
{
    const std::string dataPath = service->getProblemDataPath(problemId);
    std::error_code ec;
    fs::remove_all(dataPath, ec);

    writeTextFile(fs::path(dataPath) / "1.in", "1 2\n");
    writeTextFile(fs::path(dataPath) / "1.out", "3\n");
    writeTextFile(fs::path(dataPath) / "2.in", "5 6\n");
    writeTextFile(fs::path(dataPath) / "2.out", "11\n");
}

void assertStatusOnly(const std::string& caseName,
                      const Json::Value& result,
                      int expectedStatus,
                      Json::ArrayIndex expectedTestCount,
                      bool expectCompilerInfoEmpty)
{
    validateTopLevel(result);
    require(result["Status"].asInt() == expectedStatus, caseName + ": unexpected top-level status");
    require(result["TestInfo"].size() == expectedTestCount, caseName + ": unexpected TestInfo size");

    if (expectCompilerInfoEmpty)
    {
        require(result["ComplierInfo"].asString().empty(), caseName + ": ComplierInfo must be empty");
    }
    else
    {
        require(!result["ComplierInfo"].asString().empty(), caseName + ": ComplierInfo must be non-empty");
    }

    for (Json::ArrayIndex i = 0; i < result["TestInfo"].size(); ++i)
    {
        validateTestItem(result["TestInfo"][i]);
        require(result["TestInfo"][i]["Status"].asInt() == expectedStatus,
                caseName + ": unexpected case status");
    }
}

void assertAc(const std::string& caseName, const Json::Value& result)
{
    validateTopLevel(result);
    require(result["Status"].asInt() == 2, caseName + ": status must be AC");
    require(result["ComplierInfo"].asString().empty(), caseName + ": ComplierInfo must be empty");
    require(result["TestInfo"].size() == 2, caseName + ": TestInfo size");

    validateTestItem(result["TestInfo"][0]);
    validateTestItem(result["TestInfo"][1]);

    require(result["TestInfo"][0]["Status"].asInt() == 2, caseName + ": case1 status");
    require(result["TestInfo"][1]["Status"].asInt() == 2, caseName + ": case2 status");
    require(result["TestInfo"][0]["StandardInput"].asString() == "1 2\n", caseName + ": case1 StandardInput");
    require(result["TestInfo"][0]["StandardOutput"].asString() == "3\n", caseName + ": case1 StandardOutput");
    require(result["TestInfo"][0]["PersonalOutput"].asString() == "3\n", caseName + ": case1 PersonalOutput");
    require(result["TestInfo"][1]["StandardInput"].asString() == "5 6\n", caseName + ": case2 StandardInput");
    require(result["TestInfo"][1]["StandardOutput"].asString() == "11\n", caseName + ": case2 StandardOutput");
    require(result["TestInfo"][1]["PersonalOutput"].asString() == "11\n", caseName + ": case2 PersonalOutput");
}

void assertWa(const std::string& caseName, const Json::Value& result)
{
    validateTopLevel(result);
    require(result["Status"].asInt() == 3, caseName + ": status must be WA");
    require(result["ComplierInfo"].asString().empty(), caseName + ": ComplierInfo must be empty");
    require(result["TestInfo"].size() == 2, caseName + ": TestInfo size");

    validateTestItem(result["TestInfo"][0]);
    validateTestItem(result["TestInfo"][1]);

    require(result["TestInfo"][0]["Status"].asInt() == 2, caseName + ": case1 status");
    require(result["TestInfo"][1]["Status"].asInt() == 3, caseName + ": case2 status");
    require(result["TestInfo"][0]["PersonalOutput"].asString() == "3\n", caseName + ": case1 PersonalOutput");
    require(result["TestInfo"][1]["PersonalOutput"].asString() == "10\n", caseName + ": case2 PersonalOutput");
}

void runAndAssert(SandboxService* service,
                  const std::string& caseName,
                  const Json::Value& task,
                  void (*assertFn)(const std::string&, const Json::Value&))
{
    if (!shouldRunCase(caseName))
    {
        return;
    }
    const Json::Value result = service->submitTask(task);
    dumpResultIfNeeded(caseName, result);
    assertFn(caseName, result);
    std::cout << "[PASS] " << caseName << std::endl;
}

void assertCe(const std::string& caseName, const Json::Value& result)
{
    assertStatusOnly(caseName, result, 1, 0, false);
}

void assertRe(const std::string& caseName, const Json::Value& result)
{
    assertStatusOnly(caseName, result, 4, 1, true);
}

void assertTle(const std::string& caseName, const Json::Value& result)
{
    assertStatusOnly(caseName, result, 5, 1, true);
}

void assertMle(const std::string& caseName, const Json::Value& result)
{
    assertStatusOnly(caseName, result, 6, 1, true);
}

void assertSe(const std::string& caseName, const Json::Value& result)
{
    assertStatusOnly(caseName, result, 7, 1, false);
}

std::vector<LanguagePrograms> buildPrograms()
{
    std::vector<LanguagePrograms> programs;

    programs.push_back(LanguagePrograms{
        "C",
        "C",
        "#include <stdio.h>\nint main(){ printf(\"hello\") return 0; }\n",
        "#include <stdio.h>\nint main(){ long long a,b; if(scanf(\"%lld%lld\", &a, &b) != 2) return 0; printf(\"%lld\\n\", a + b); return 0; }\n",
        "#include <stdio.h>\nint main(){ long long a,b; if(scanf(\"%lld%lld\", &a, &b) != 2) return 0; if(a==5&&b==6) printf(\"10\\n\"); else printf(\"%lld\\n\", a + b); return 0; }\n",
        "int main(){ int *p = 0; *p = 1; return 0; }\n",
        "int main(){ while(1){} return 0; }\n",
        "#include <stdio.h>\n#include <stdlib.h>\n#include <string.h>\nint main(){ while(1){ void* p = malloc(16 * 1024 * 1024); if(!p){ fprintf(stderr, \"cannot allocate memory\\n\"); return 1; } memset(p, 1, 16 * 1024 * 1024); } }\n"
    });

    programs.push_back(LanguagePrograms{
        "CPP",
        "C++",
        "#include <bits/stdc++.h>\nint main(){ std::cout << \"hello\" << std::endl return 0; }\n",
        "#include <bits/stdc++.h>\nint main(){ long long a,b; if(!(std::cin>>a>>b)) return 0; std::cout<<(a+b)<<\"\\n\"; }\n",
        "#include <bits/stdc++.h>\nint main(){ long long a,b; if(!(std::cin>>a>>b)) return 0; if(a==5&&b==6) std::cout<<10<<\"\\n\"; else std::cout<<(a+b)<<\"\\n\"; }\n",
        "#include <bits/stdc++.h>\nint main(){ int* p=nullptr; *p=1; }\n",
        "#include <bits/stdc++.h>\nint main(){ while(true){} }\n",
        "#include <bits/stdc++.h>\nint main(){ std::vector<char*> blocks; try { while(true){ char* p = new char[16 * 1024 * 1024]; for(int i = 0; i < 16 * 1024 * 1024; i += 4096){ p[i] = 1; } blocks.push_back(p); } } catch (const std::bad_alloc&) { std::cerr << \"std::bad_alloc\\n\"; return 1; } }\n"
    });

    programs.push_back(LanguagePrograms{
        "JAVA",
        "Java",
        "public class Main { public static void main(String[] args) { System.out.println(\"hello\") } }\n",
        "import java.util.*;\npublic class Main { public static void main(String[] args) { Scanner in = new Scanner(System.in); if(!in.hasNextLong()) return; long a = in.nextLong(); long b = in.nextLong(); System.out.println(a + b); } }\n",
        "import java.util.*;\npublic class Main { public static void main(String[] args) { Scanner in = new Scanner(System.in); if(!in.hasNextLong()) return; long a = in.nextLong(); long b = in.nextLong(); if(a == 5 && b == 6) System.out.println(10); else System.out.println(a + b); } }\n",
        "public class Main { public static void main(String[] args) { int x = 1 / 0; System.out.println(x); } }\n",
        "public class Main { public static void main(String[] args) { while(true){} } }\n",
        "import java.util.*;\npublic class Main { public static void main(String[] args) { List<byte[]> list = new ArrayList<>(); try { while(true){ list.add(new byte[16 * 1024 * 1024]); } } catch (OutOfMemoryError e) { System.err.println(\"OutOfMemoryError\"); System.exit(1); } } }\n"
    });

    programs.push_back(LanguagePrograms{
        "PY",
        "Python3",
        "def main()\n    print(\"hello\")\n\nif __name__ == '__main__':\n    main()\n",
        "import sys\n\ndef main():\n    data = sys.stdin.read().strip().split()\n    if len(data) < 2:\n        return\n    a, b = map(int, data[:2])\n    print(a + b)\n\nif __name__ == '__main__':\n    main()\n",
        "import sys\n\ndef main():\n    data = sys.stdin.read().strip().split()\n    if len(data) < 2:\n        return\n    a, b = map(int, data[:2])\n    if a == 5 and b == 6:\n        print(10)\n    else:\n        print(a + b)\n\nif __name__ == '__main__':\n    main()\n",
        "print(1 // 0)\n",
        "while True:\n    pass\n",
        "import sys\n\narr = []\ntry:\n    while True:\n        arr.append(bytearray(16 * 1024 * 1024))\nexcept MemoryError:\n    sys.stderr.write('MemoryError\\n')\n    raise SystemExit(1)\n"
    });

    return programs;
}

}

int main()
{
    setenv("XDOJ_PROBLEMDATA_ROOT", "/tmp/xdoj-problemdata", 1);
    setenv("XDOJ_CGROUP_ROOT", "/sys/fs/cgroup/xdoj", 1);

    unsetenv("XDOJ_CGROUP_STRICT");
    // unsetenv("XDOJ_CGROUP_ROOT");
    unsetenv("XDOJ_NAMESPACE_STRICT");

    SandboxService* service = SandboxService::GetInstance();
    const std::string problemId = "autotest-2001";
    prepareProblemData(service, problemId);

    const std::vector<LanguagePrograms> programs = buildPrograms();

    for (const LanguagePrograms& program : programs)
    {
        const bool isJava = (program.language == "Java");
        const int normalMemMb = isJava ? 2048 : 256;
        const int tleTimeMs = isJava ? 200 : 50;
        const int mleMemMb = isJava ? 2048 : 32;

        runAndAssert(service,
                     program.tag + "-CE",
                     buildTask(program.tag + "_ce", problemId, program.language, program.ce, 1, 1000, normalMemMb),
                     assertCe);

        runAndAssert(service,
                     program.tag + "-AC",
                     buildTask(program.tag + "_ac", problemId, program.language, program.ac, 2, 1000, normalMemMb),
                     assertAc);

        runAndAssert(service,
                     program.tag + "-WA",
                     buildTask(program.tag + "_wa", problemId, program.language, program.wa, 2, 1000, normalMemMb),
                     assertWa);

        runAndAssert(service,
                     program.tag + "-RE",
                     buildTask(program.tag + "_re", problemId, program.language, program.re, 1, 1000, normalMemMb),
                     assertRe);

        runAndAssert(service,
                     program.tag + "-TLE",
                     buildTask(program.tag + "_tle", problemId, program.language, program.tle, 1, tleTimeMs, normalMemMb),
                     assertTle);

        runAndAssert(service,
                     program.tag + "-MLE",
                     buildTask(program.tag + "_mle", problemId, program.language, program.mle, 1, 1000, mleMemMb),
                     assertMle);

        {
            EnvGuard strictGuard("XDOJ_CGROUP_STRICT");
            EnvGuard rootGuard("XDOJ_CGROUP_ROOT");
            setenv("XDOJ_CGROUP_STRICT", "1", 1);
            setenv("XDOJ_CGROUP_ROOT", "/proc/xdoj-invalid-cgroup-root", 1);

            runAndAssert(service,
                         program.tag + "-SE",
                         buildTask(program.tag + "_se", problemId, program.language, program.ac, 1, 1000, normalMemMb),
                         assertSe);
        }
    }

    std::cout << "ALL TESTS PASSED" << std::endl;
    return 0;
}
