/*
 * qwrt — Debug core API (step debugger)
 *
 * PAL-agnostic. Provides breakpoint/step/pause and call-frame/local
 * introspection on top of the QuickJS-ng debugger engine patch
 * (deps/quickjs-ng-debugger.patch). The DAP protocol front-end
 * (qwrt_debug_dap.h) builds on this; hosts may also drive it directly.
 *
 * Compiled in only when QWRT_BUILD_DEBUGGER=ON (which defines
 * QWRT_DEBUG_SUPPORT). Otherwise this header's functions are absent.
 */
#ifndef QWRT_DEBUG_H
#define QWRT_DEBUG_H

#include <qwrt/qwrt.h>

#ifdef __cplusplus
extern "C" {
#endif

#ifdef QWRT_DEBUG_SUPPORT

/* Opaque debug session. */
typedef struct qwrt_debug qwrt_debug_t;

/* Callbacks the host registers. All are invoked from within the paused
 * interrupt handler (same thread as JS). */
typedef struct qwrt_debug_cbs {
    /* The session has stopped (breakpoint / step / pause / entry). The host
     * pumps its protocol until qwrt_debug_continue / step_* is called, at
     * which point this returns and JS resumes. reason is one of:
     *   "breakpoint", "step", "pause", "entry". */
    void (*on_stopped)(qwrt_debug_t *dbg, const char *reason, int thread_id);
    /* The debuggee finished (the JS eval that was being driven returned). */
    void (*on_terminated)(qwrt_debug_t *dbg);
    /* A console output line was produced by JS. Optional (may be NULL). */
    void (*on_output)(qwrt_debug_t *dbg, const char *category, const char *text);
} qwrt_debug_cbs;

/* Attach a debugger to an existing runtime. Installs the engine debugger
 * hooks + interrupt handler. Must be called before JS runs. Returns the
 * session handle or NULL on error. */
qwrt_debug_t *qwrt_debug_attach(qwrt_t *rt, const qwrt_debug_cbs *cbs);

/* Detach and free the session. JS resumes normally afterwards. */
void qwrt_debug_detach(qwrt_t *rt, qwrt_debug_t *dbg);

/* Breakpoint table. filename is matched against the filename atom used in
 * JS_Eval (the host must eval with the real source path). */
int  qwrt_debug_add_breakpoint(qwrt_debug_t *dbg, const char *filename,
                               int line, const char *condition);
int  qwrt_debug_remove_breakpoint(qwrt_debug_t *dbg, const char *filename, int line);
void qwrt_debug_clear_breakpoints(qwrt_debug_t *dbg);

/* Flow control. Called by the host while inside on_stopped (i.e. from within
 * the paused interrupt handler). They set the step mode and return, causing
 * on_stopped to return, causing the interrupt handler to return 0, causing
 * JS to resume. */
void qwrt_debug_continue(qwrt_debug_t *dbg);
void qwrt_debug_pause(qwrt_debug_t *dbg);
void qwrt_debug_step_over(qwrt_debug_t *dbg);
void qwrt_debug_step_into(qwrt_debug_t *dbg);
void qwrt_debug_step_out(qwrt_debug_t *dbg);

/* Pause immediately at the next dispatch (used for "stop on entry"). */
void qwrt_debug_stop_on_entry(qwrt_debug_t *dbg);

typedef struct qwrt_debug_var {
    char *name;
    char *value_json;   /* JSON string of the value; NULL if unreadable. Caller frees. */
    char *type;         /* typeof string, or NULL. Caller frees. */
    int   variables_reference; /* 0 = leaf; >0 = expandable. */
    int   indexed_variables;
} qwrt_debug_var;

typedef struct qwrt_debug_frame {
    char *name;        /* function name */
    char *source_path; /* filename given to JS_Eval */
    int   line;
    int   column;
    int   id;          /* stable id for scopes/variables/evaluate */
} qwrt_debug_frame;

int  qwrt_debug_get_call_frames(qwrt_debug_t *dbg,
                                qwrt_debug_frame **out_frames, int *out_count);
void qwrt_debug_free_frames(qwrt_debug_frame *frames, int count);

typedef struct qwrt_debug_scope {
    char *name;        /* "Locals", "Arguments", "Global" */
    int   variables_reference;
    int   expensive;
} qwrt_debug_scope;

int  qwrt_debug_get_scopes(qwrt_debug_t *dbg, int frame_id,
                           qwrt_debug_scope **out_scopes, int *out_count);
void qwrt_debug_free_scopes(qwrt_debug_scope *scopes, int count);

int  qwrt_debug_get_variables(qwrt_debug_t *dbg, int variables_reference,
                              qwrt_debug_var **out_vars, int *out_count);
void qwrt_debug_free_vars(qwrt_debug_var *vars, int count);

int  qwrt_debug_evaluate(qwrt_debug_t *dbg, int frame_id,
                         const char *expression,
                         char **out_value_json, char **out_error);

#endif /* QWRT_DEBUG_SUPPORT */

#ifdef __cplusplus
}
#endif
#endif /* QWRT_DEBUG_H */
