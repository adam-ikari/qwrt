/*
 * qwrt bytecode example.
 *
 * Demonstrates ahead-of-time compilation: qwrt_compile() turns JS source
 * into QuickJS bytecode, which qwrt_eval_bytecode() then runs. Compiling
 * once and re-evaluating the same bytes across fresh contexts amortizes
 * parse cost — useful for hosts that run the same code many times.
 *
 * Uses the mock PAL so it builds and runs with no libuv / network.
 *
 * Build: cmake -B build -DQWRT_BUILD_EXAMPLES=ON
 * Run:   ./build/examples/example_bytecode
 */
#include <qwrt/qwrt.h>
#include <pal_mock.h>
#include <stdio.h>
#include <string.h>

/* A small module that defines a function, then exports its result for a
 * given argument. We compile this once and re-run it with different inputs. */
static const char *SRC =
    "function square(x) { return x * x; } "
    "square(%d)";

int main(void) {
    qwrt_pal_t *pal = pal_mock_create();
    if (!pal) {
        fprintf(stderr, "failed to create pal_mock\n");
        return 1;
    }

    qwrt_config_t config = { .pal = pal, .debug = 0 };

    /* Compile a template on a throwaway runtime. */
    qwrt_t *compiler = qwrt_create(&config);
    if (!compiler) {
        fprintf(stderr, "failed to create compiler runtime\n");
        pal_mock_destroy(pal);
        return 1;
    }

    /* Compile a parametrized snippet for each input (different source bytes
     * per arg, but the point is to show compile -> eval_bytecode separation). */
    int inputs[] = { 3, 7, 12 };
    for (int i = 0; i < 3; ++i) {
        char src[64];
        snprintf(src, sizeof(src), SRC, inputs[i]);

        size_t bc_len = 0;
        uint8_t *bc = qwrt_compile(compiler, src, strlen(src), &bc_len);
        if (!bc) {
            fprintf(stderr, "qwrt_compile failed for input %d\n", inputs[i]);
            continue;
        }

        /* Evaluate the bytecode in a fresh runtime. */
        qwrt_t *rt = qwrt_create(&config);
        if (!rt) { qwrt_free(bc); continue; }

        char *result = NULL;
        if (qwrt_eval_bytecode(rt, bc, bc_len, &result) == 0 && result) {
            printf("square(%d) = %s (%zu bc bytes)\n", inputs[i], result, bc_len);
            qwrt_free(result);
        }
        qwrt_destroy(rt);
        qwrt_free(bc);
    }

    qwrt_destroy(compiler);
    pal_mock_destroy(pal);
    return 0;
}
