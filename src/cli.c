/*
 * qwrt CLI — standalone WinterTC runtime
 * 用法: qwrt [options] [script.js [args...]] | qwrt -e 'code'
 *       qwrt（无参数）→ REPL
 */
#include <qwrt/qwrt.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#define QWRT_CLI_VERSION "qwrt 0.1.0"

static void usage(FILE *out) {
    fprintf(out,
        "Usage: qwrt [options] [script.js [args...]]\n"
        "       qwrt -e 'code' [args...]\n"
        "       qwrt            (start REPL)\n"
        "\n"
        "Options:\n"
        "  -e, --eval <code>   evaluate <code> and exit\n"
        "  -h, --help          show this help\n"
        "  -v, --version       show version\n"
        "\n"
        "Runs a WinterTC-compatible JavaScript runtime (fetch, crypto,\n"
        "streams, timers, fs, ...). No Node.js APIs (process, require, ...).\n");
}

int main(int argc, char **argv) {
    /* 解析 -h/-v 与 -e；第一个非 flag 参数 = 脚本路径；其余 = 脚本参数 */
    for (int i = 1; i < argc; i++) {
        if (!strcmp(argv[i], "-h") || !strcmp(argv[i], "--help")) {
            usage(stdout);
            return 0;
        }
        if (!strcmp(argv[i], "-v") || !strcmp(argv[i], "--version")) {
            printf("%s\n", QWRT_CLI_VERSION);
            return 0;
        }
        if (!strcmp(argv[i], "-e") || !strcmp(argv[i], "--eval")) {
            if (i + 1 >= argc) { usage(stderr); return 2; }
            /* Task 5: eval 模式 */
        }
        /* 未知 flag */
        if (argv[i][0] == '-') { usage(stderr); return 2; }
        /* Task 4: 脚本模式 */
    }
    /* Task 6: REPL */
    return 0;
}