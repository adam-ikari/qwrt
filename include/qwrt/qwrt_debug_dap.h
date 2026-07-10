/*
 * qwrt — DAP (Debug Adapter Protocol) front-end
 *
 * PAL-agnostic, lives in libqwrt. Provides the DAP callback set that
 * qwrt_create installs when debugging is enabled (config field or QWRT_DEBUG
 * env var). Speaks DAP base protocol over stdio so VS Code can attach.
 *
 * The host does NOT call a "serve" entry point: qwrt_create auto-attaches
 * this layer when debug is enabled, and the DAP stdin pump runs inside the
 * on_stopped callback when a pause occurs.
 *
 * Compiled in only when QWRT_BUILD_DEBUGGER=ON (QWRT_DEBUG_SUPPORT).
 */
#ifndef QWRT_DEBUG_DAP_H
#define QWRT_DEBUG_DAP_H

#include <qwrt/qwrt.h>
#include <stdio.h>

#ifdef __cplusplus
extern "C" {
#endif

#ifdef QWRT_DEBUG_SUPPORT

typedef struct qwrt_dap_config {
    int  stop_on_entry;   /* default 1 (attach semantics: pause at entry) */
    FILE *in;             /* default stdin if NULL */
    FILE *out;            /* default stdout if NULL */
} qwrt_dap_config_t;

/* Attach the DAP protocol layer to a runtime. Called by qwrt_create when
 * debug is enabled (config.debug enable-bit or QWRT_DEBUG env var); may also
 * be called directly by a host that wants DAP without the auto path.
 * Installs qwrt_debug_attach with the DAP callback set, sends the DAP
 * "initialized" event, and sets up entry-pause per stop_on_entry. Returns 0
 * on success, non-zero on error. Non-blocking — the DAP stdin pump runs
 * later inside on_stopped when JS pauses. */
int qwrt_dap_attach(qwrt_t *rt, const qwrt_dap_config_t *cfg);

/* Detach the DAP layer (counterpart to qwrt_dap_attach). */
void qwrt_dap_detach(qwrt_t *rt);

/* Process the DAP configuration phase (initialize / setBreakpoints / attach /
 * configurationDone) before the program starts running. Called by the host
 * after qwrt_dap_attach. Returns 0 when configurationDone is received, 1 if
 * the client disconnected, -1 on EOF/error. After 0, the host evals/runs its
 * program; breakpoints are armed and the on_stopped pump takes over. */
int qwrt_dap_configure(qwrt_t *rt);

#endif /* QWRT_DEBUG_SUPPORT */

#ifdef __cplusplus
}
#endif
#endif /* QWRT_DEBUG_DAP_H */
