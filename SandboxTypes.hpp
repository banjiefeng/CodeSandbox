#pragma once

#include <jsoncpp/json/json.h>

#include <cstdint>
#include <string>
#include <vector>

enum class JudgeStatus : int
{
    CE = 1,  // Compile Error
    AC = 2,  // Accepted
    WA = 3,  // Wrong Answer
    RE = 4,  // Runtime Error
    TLE = 5, // Time Limit Exceeded
    MLE = 6, // Memory Limit Exceeded
    SE = 7   // System Error
};

inline int toStatusCode(JudgeStatus status)
{
    return static_cast<int>(status);
}

struct SandboxTask {
    std::string submitId;
    std::string problemId;
    std::string code;
    std::string language;
    int timeLimit = 0;
    std::int64_t memoryLimit = 0;
    int testNum = 0;
    std::string dataPath;
};

struct SandboxTestCase {
    int index = 0;
    std::string inputPath;
    std::string outputPath;
};

struct SandboxCompileResult {
    bool success = true;
    std::string message;
};

struct SandboxWorkspaceLayout {
    std::string rootDir;
    std::string rootfsDir;
    std::string workDir;
    std::string sourcePath;
    std::string executablePath;
    std::string compileStdoutPath;
    std::string compileStderrPath;
    std::string runStdoutPath;
    std::string runStderrPath;
};

struct SandboxInstance {
    std::size_t slotId = 0;
    std::string rootDir;
};

struct SandboxPreparedTask {
    SandboxTask task;
    const class LanguageRunner* runner = nullptr;
    std::vector<SandboxTestCase> testCases;
    bool hasSpj = false;
    std::string spjSourcePath;
    std::string spjExecutablePath;
    SandboxWorkspaceLayout buildLayout;
    SandboxWorkspaceLayout runtimeLayout;
};

struct SandboxCaseResult {
    JudgeStatus status = JudgeStatus::SE;
    std::string standardInput;
    std::string standardOutput;
    std::string personalOutput;
    int runTime = 0;
    std::int64_t runMemory = 0;
};

struct SandboxRunResult {
    JudgeStatus status = JudgeStatus::SE;
    int runTime = 0;
    std::int64_t runMemory = 0;
    std::string message;
    std::vector<SandboxCaseResult> caseResults;
};

struct SandboxFinalResult {
    SandboxTask task;
    JudgeStatus status = JudgeStatus::SE;
    int runTime = 0;
    std::int64_t runMemory = 0;
    int length = 0;
    std::string compilerInfo;
    std::vector<SandboxCaseResult> testInfo;

    Json::Value toJson() const {
        Json::Value result;
        result["SubmitId"] = task.submitId;
        result["Status"] = toStatusCode(status);
        result["RunTime"] = std::to_string(runTime) + "MS";
        result["RunMemory"] = std::to_string(runMemory / (1024 * 1024)) + "MB";
        result["Length"] = std::to_string(length) + "B";
        result["ComplierInfo"] = compilerInfo;

        Json::Value tests(Json::arrayValue);
        for (const SandboxCaseResult& caseResult : testInfo) {
            Json::Value item;
            item["Status"] = toStatusCode(caseResult.status);
            // 为了保证返回结构稳定，StandardInput 字段总是存在（即使为空）。
            item["StandardInput"] = caseResult.standardInput;
            item["StandardOutput"] = caseResult.standardOutput;
            item["PersonalOutput"] = caseResult.personalOutput;
            item["RunTime"] = std::to_string(caseResult.runTime) + "MS";
            item["RunMemory"] = std::to_string(caseResult.runMemory / (1024 * 1024)) + "MB";
            tests.append(item);
        }
        result["TestInfo"] = tests;
        return result;
    }
};
