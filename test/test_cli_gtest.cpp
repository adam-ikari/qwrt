// test_cli_gtest.cpp — qwrt CLI 端到端（fork 子进程断言 stdout/stderr/退出码）
//
// 驱动真实的 qwrt 可执行文件（QWRT_CLI_BIN），覆盖：
//   - 脚本 / -e / REPL 顶层执行与 console 输出流
//   - 异步任务等待（timer 触发后才退出）
//   - 脚本抛错 → 退出码 1 + stderr
//   - 缺失文件 → 退出码 1 + stderr
//   - --help / --version → stdout + 退出码 0
//   - 未知 flag → 退出码 2
//   - globalThis.arguments 注入（WinterCG proposal-cli-api 方向）
#include <gtest/gtest.h>
#include <sys/wait.h>
#include <unistd.h>

#include <cstdio>
#include <cstring>
#include <string>
#include <vector>

#ifndef QWRT_CLI_BIN
#define QWRT_CLI_BIN "qwrt"
#endif

#ifndef CLI_SCRIPTS_DIR
#define CLI_SCRIPTS_DIR "test/cli_scripts"
#endif

namespace {

struct CliResult {
    int exit_code;
    std::string out, err;
};

CliResult run_cli(const std::vector<std::string>& args) {
    int out_pipe[2], err_pipe[2];
    if (pipe(out_pipe) != 0 || pipe(err_pipe) != 0) {
        return {-1, "", ""};
    }
    pid_t pid = fork();
    if (pid == 0) {
        /* child: 重定向 stdout/stderr 到管道，exec CLI */
        dup2(out_pipe[1], STDOUT_FILENO);
        dup2(err_pipe[1], STDERR_FILENO);
        close(out_pipe[0]);
        close(out_pipe[1]);
        close(err_pipe[0]);
        close(err_pipe[1]);
        std::vector<std::string> argv0 = {QWRT_CLI_BIN};
        argv0.insert(argv0.end(), args.begin(), args.end());
        std::vector<char*> cargv;
        for (auto& a : argv0) cargv.push_back(const_cast<char*>(a.c_str()));
        cargv.push_back(nullptr);
        execv(QWRT_CLI_BIN, cargv.data());
        _exit(127); /* exec 失败 */
    }
    /* parent: 读管道 → waitpid */
    close(out_pipe[1]);
    close(err_pipe[1]);
    char buf[4096];
    ssize_t n;
    std::string out, err;
    while ((n = read(out_pipe[0], buf, sizeof buf)) > 0) out.append(buf, n);
    while ((n = read(err_pipe[0], buf, sizeof buf)) > 0) err.append(buf, n);
    close(out_pipe[0]);
    close(err_pipe[0]);
    int status = 0;
    waitpid(pid, &status, 0);
    int code = WIFEXITED(status)
                   ? WEXITSTATUS(status)
                   : (WIFSIGNALED(status) ? 128 + WTERMSIG(status) : -1);
    return {code, out, err};
}

std::string script(const char* name) {
    return std::string(CLI_SCRIPTS_DIR) + "/" + name;
}

}  // namespace

TEST(CliTest, HelloWorld) {
    auto r = run_cli({"-e", "console.log('hello from qwrt')"});
    EXPECT_EQ(0, r.exit_code);
    EXPECT_NE(std::string::npos, r.out.find("hello from qwrt"));
}

TEST(CliTest, ScriptFile) {
    auto r = run_cli({script("hello.js")});
    EXPECT_EQ(0, r.exit_code);
    EXPECT_NE(std::string::npos, r.out.find("hello from qwrt"));
}

TEST(CliTest, AsyncTimerWaits) {
    /* 顶层输出先出现，然后等 50ms timer 触发才退出 —— 验证 wait_idle 语义 */
    auto r = run_cli({script("timer.js")});
    EXPECT_EQ(0, r.exit_code);
    EXPECT_NE(std::string::npos, r.out.find("after timer setup"));
    EXPECT_NE(std::string::npos, r.out.find("timer fired"));
}

TEST(CliTest, ScriptErrorExitCode) {
    auto r = run_cli({script("throw.js")});
    EXPECT_EQ(1, r.exit_code);
    EXPECT_FALSE(r.err.empty());
}

TEST(CliTest, MissingFile) {
    auto r = run_cli({"/nonexistent/script.js"});
    EXPECT_EQ(1, r.exit_code);
    EXPECT_NE(std::string::npos, r.err.find("cannot open"));
}

TEST(CliTest, HelpExitZero) {
    auto r = run_cli({"--help"});
    EXPECT_EQ(0, r.exit_code);
    EXPECT_NE(std::string::npos, r.out.find("Usage"));
}

TEST(CliTest, VersionPrintsVersion) {
    auto r = run_cli({"--version"});
    EXPECT_EQ(0, r.exit_code);
    EXPECT_NE(std::string::npos, r.out.find("qwrt"));
}

TEST(CliTest, ArgsInjected) {
    /* WinterCG proposal-cli-api 方向：arguments 不含可执行名与脚本路径 */
    auto r = run_cli({"-e",
                      "console.log(JSON.stringify(globalThis.arguments))",
                      "a", "b"});
    EXPECT_EQ(0, r.exit_code);
    EXPECT_NE(std::string::npos, r.out.find("[\"a\",\"b\"]"));
}

TEST(CliTest, UnknownFlag) {
    auto r = run_cli({"--bogus"});
    EXPECT_EQ(2, r.exit_code);
}
