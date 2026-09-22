/*
 * amoib — Debug core API (step debugger)
 *
 * PAL-agnostic. Provides breakpoint/step/pause and call-frame/local
 * introspection on top of the QuickJS-ng debugger engine patch
 * (deps/quickjs-ng-debugger.patch). The DAP protocol front-end
 * (am_debug_dap.h) builds on this; hosts may also drive it directly.
 *
 * Compiled in only when AM_BUILD_DEBUGGER=ON (which defines
 * AM_DEBUG_SUPPORT). Otherwise this header's functions are absent.
 */
#ifndef AM_DEBUG_H
#define AM_DEBUG_H

#include <amoib/amoib.h>

#ifdef __cplusplus
extern "C" {
#endif

#ifdef AM_DEBUG_SUPPORT

/* Opaque debug session. */
typedef struct am_debug am_debug_t;

/* Callbacks the host registers. All are invoked from within the paused
 * interrupt handler (same thread as JS). */
typedef struct am_debug_cbs {
    /* The session has stopped (breakpoint / step / pause / entry). The host
     * pumps its protocol until am_debug_continue / step_* is called, at
     * which point this returns and JS resumes. reason is one of:
     *   "breakpoint", "step", "pause", "entry". */
    void (*on_stopped)(am_debug_t *dbg, const char *reason, int thread_id);
    /* The debuggee finished (the JS eval that was being driven returned). */
    void (*on_terminated)(am_debug_t *dbg);
    /* A console output line was produced by JS. Optional (may be NULL). */
    void (*on_output)(am_debug_t *dbg, const char *category, const char *text);
} am_debug_cbs;

/* Attach a debugger to an existing runtime. Installs the engine debugger
 * hooks + interrupt handler. Must be called before JS runs. Returns the
 * session handle or NULL on error. */
am_debug_t *am_debug_attach(am_t *rt, const am_debug_cbs *cbs);

/* Return the runtime a debug session is attached to. Lets callback
 * implementations (e.g. the DAP layer) reach their per-runtime state
 * without a process-wide global. */
am_t *am_debug_get_runtime(am_debug_t *dbg);

/* Detach and free the session. JS resumes normally afterwards. */
void am_debug_detach(am_t *rt, am_debug_t *dbg);

/* Breakpoint table. filename is matched against the filename atom used in
 * JS_Eval (the host must eval with the real source path). */
int  am_debug_add_breakpoint(am_debug_t *dbg, const char *filename,
                               int line, const char *condition);
int  am_debug_remove_breakpoint(am_debug_t *dbg, const char *filename, int line);
void am_debug_clear_breakpoints(am_debug_t *dbg);

/* Flow control. Called by the host while inside on_stopped (i.e. from within
 * the paused interrupt handler). They set the step mode and return, causing
 * on_stopped to return, causing the interrupt handler to return 0, causing
 * JS to resume. */
void am_debug_continue(am_debug_t *dbg);
void am_debug_pause(am_debug_t *dbg);
void am_debug_step_over(am_debug_t *dbg);
void am_debug_step_into(am_debug_t *dbg);
void am_debug_step_out(am_debug_t *dbg);

/* Pause immediately at the next dispatch (used for "stop on entry"). */
void am_debug_stop_on_entry(am_debug_t *dbg);

typedef struct am_debug_var {
    char *name;
    char *value_json;   /* JSON string of the value; NULL if unreadable. Caller frees. */
    char *type;         /* typeof string, or NULL. Caller frees. */
    int   variables_reference; /* 0 = leaf; >0 = expandable. */
    int   indexed_variables;
} am_debug_var;

typedef struct am_debug_frame {
    char *name;        /* function name */
    char *source_path; /* filename given to JS_Eval */
    int   line;
    int   column;
    int   id;          /* stable id for scopes/variables/evaluate */
} am_debug_frame;

int  am_debug_get_call_frames(am_debug_t *dbg,
                                am_debug_frame **out_frames, int *out_count);
void am_debug_free_frames(am_debug_frame *frames, int count);

typedef struct am_debug_scope {
    char *name;        /* "Locals", "Arguments", "Global" */
    int   variables_reference;
    int   expensive;
} am_debug_scope;

int  am_debug_get_scopes(am_debug_t *dbg, int frame_id,
                           am_debug_scope **out_scopes, int *out_count);
void am_debug_free_scopes(am_debug_scope *scopes, int count);

int  am_debug_get_variables(am_debug_t *dbg, int variables_reference,
                              am_debug_var **out_vars, int *out_count);
void am_debug_free_vars(am_debug_var *vars, int count);

int  am_debug_evaluate(am_debug_t *dbg, int frame_id,
                         const char *expression,
                         char **out_value_json, char **out_error);

#endif /* AM_DEBUG_SUPPORT */

#ifdef __cplusplus
}
#endif
#endif /* AM_DEBUG_H */
