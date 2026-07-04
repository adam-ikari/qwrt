/*
 * qwrt ACE Tools Extension
 *
 * Native implementations of core agent tools (read/write/edit/glob/grep/bash).
 * Registers pal.aceRead, pal.aceWrite, pal.aceEdit, pal.aceGlob, pal.aceGrep,
 * pal.aceBash on the JS pal object.
 *
 * These functions use C stdio/posix directly for maximum performance,
 * bypassing the PAL fs bridge. This is intentional — tools are the
 * performance-critical path, and C-level access avoids the overhead
 * of PAL → Promise → JS → Promise → JS chains.
 *
 * When QWRT_WITH_ACE_TOOLS is not defined, the extension compiles but
 * does nothing — the TypeScript tool factories will fall back to
 * runtime.fs (Node.js environment).
 */

#include "qwrt_internal.h"

#ifdef QWRT_WITH_ACE_TOOLS

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <dirent.h>
#include <sys/stat.h>
#include <errno.h>
#include <limits.h>

/* Maximum path length for tool operations.  Avoids PATH_MAX (4096)
 * to keep stack allocations reasonable, especially for ESP32-S3. */
#define ACE_TOOL_PATH_MAX 2048
#define ACE_TOOL_READ_BUF  4096  /* fgets buffer for aceBash */

/* ================================================================
 * Helper: create a JS object from C key-value pairs
 * ================================================================ */

static JSValue make_result_object(JSContext *ctx, int ok,
                                  const char *error_msg,
                                  const char *content,
                                  int count)
{
    JSValue obj = JS_NewObject(ctx);
    JS_SetPropertyStr(ctx, obj, "success", JS_NewBool(ctx, ok));
    if (error_msg)
        JS_SetPropertyStr(ctx, obj, "error", JS_NewString(ctx, error_msg));
    if (content)
        JS_SetPropertyStr(ctx, obj, "content", JS_NewString(ctx, content));
    if (count >= 0)
        JS_SetPropertyStr(ctx, obj, "count", JS_NewInt32(ctx, count));
    return obj;
}

/* ================================================================
 * Helper: read entire file into malloc'd buffer
 * ================================================================ */

static char *read_entire_file(const char *path, size_t *out_len)
{
    FILE *f = fopen(path, "rb");
    if (!f) return NULL;
    fseek(f, 0, SEEK_END);
    long len = ftell(f);
    fseek(f, 0, SEEK_SET);
    char *buf = (char *)malloc((size_t)len + 1);
    if (!buf) { fclose(f); return NULL; }
    size_t n = fread(buf, 1, (size_t)len, f);
    buf[n] = '\0';
    fclose(f);
    if (out_len) *out_len = n;
    return buf;
}

/* ================================================================
 * Helper: mkdir -p (recursive)
 * ================================================================ */

static int mkdirp(const char *path)
{
    char tmp[ACE_TOOL_PATH_MAX];
    snprintf(tmp, sizeof(tmp), "%s", path);
    size_t len = strlen(tmp);
    if (len > 0 && tmp[len - 1] == '/') tmp[len - 1] = '\0';

    for (char *p = tmp + 1; *p; p++) {
        if (*p == '/') {
            *p = '\0';
            mkdir(tmp, 0755);
            *p = '/';
        }
    }
    return mkdir(tmp, 0755);
}

/* ================================================================
 * aceRead — Read file contents
 * pal.aceRead(path) → { success, content, error }
 * ================================================================ */

static JSValue js_ace_read(JSContext *ctx, JSValueConst this_val,
                           int argc, JSValueConst *argv)
{
    (void)this_val;
    if (argc < 1) return JS_ThrowTypeError(ctx, "aceRead: path required");

    const char *path = JS_ToCString(ctx, argv[0]);
    if (!path) return JS_ThrowTypeError(ctx, "aceRead: path must be string");

    size_t len = 0;
    char *content = read_entire_file(path, &len);
    JS_FreeCString(ctx, path);

    if (!content) {
        return make_result_object(ctx, 0, "File not found or unreadable", NULL, -1);
    }

    JSValue result = make_result_object(ctx, 1, NULL, content, -1);
    JS_SetPropertyStr(ctx, result, "size", JS_NewInt32(ctx, (int32_t)len));
    free(content);
    return result;
}

/* ================================================================
 * aceWrite — Write content to file (creates dirs)
 * pal.aceWrite(path, content) → { success, bytes, error }
 * ================================================================ */

static JSValue js_ace_write(JSContext *ctx, JSValueConst this_val,
                            int argc, JSValueConst *argv)
{
    (void)this_val;
    if (argc < 2) return JS_ThrowTypeError(ctx, "aceWrite: path and content required");

    const char *path = JS_ToCString(ctx, argv[0]);
    const char *content = JS_ToCString(ctx, argv[1]);
    if (!path || !content) {
        if (path) JS_FreeCString(ctx, path);
        return JS_ThrowTypeError(ctx, "aceWrite: path and content must be strings");
    }

    /* Create parent directory */
    char dir[ACE_TOOL_PATH_MAX];
    snprintf(dir, sizeof(dir), "%s", path);
    char *last_slash = strrchr(dir, '/');
    if (last_slash && last_slash != dir) {
        *last_slash = '\0';
        struct stat st;
        if (stat(dir, &st) != 0) mkdirp(dir);
    }

    size_t content_len = strlen(content);
    FILE *f = fopen(path, "wb");
    if (!f) {
        JS_FreeCString(ctx, path);
        JS_FreeCString(ctx, content);
        char err[256];
        snprintf(err, sizeof(err), "Cannot write file: %s", strerror(errno));
        return make_result_object(ctx, 0, err, NULL, -1);
    }

    fwrite(content, 1, content_len, f);
    fclose(f);

    JS_FreeCString(ctx, path);
    JS_FreeCString(ctx, content);

    JSValue result = make_result_object(ctx, 1, NULL, NULL, -1);
    JS_SetPropertyStr(ctx, result, "bytes", JS_NewInt32(ctx, (int32_t)content_len));
    return result;
}

/* ================================================================
 * aceEdit — Find and replace in file
 * pal.aceEdit(path, oldText, newText, replaceAll?) → { success, replacements, error }
 * ================================================================ */

static JSValue js_ace_edit(JSContext *ctx, JSValueConst this_val,
                           int argc, JSValueConst *argv)
{
    (void)this_val;
    if (argc < 3) return JS_ThrowTypeError(ctx, "aceEdit: path, oldText, newText required");

    const char *path = JS_ToCString(ctx, argv[0]);
    const char *old_text = JS_ToCString(ctx, argv[1]);
    const char *new_text = JS_ToCString(ctx, argv[2]);
    int replace_all = (argc >= 4) ? JS_ToBool(ctx, argv[3]) : 0;

    if (!path || !old_text || !new_text) {
        if (path) JS_FreeCString(ctx, path);
        if (old_text) JS_FreeCString(ctx, old_text);
        if (new_text) JS_FreeCString(ctx, new_text);
        return JS_ThrowTypeError(ctx, "aceEdit: arguments must be strings");
    }

    size_t len = 0;
    char *content = read_entire_file(path, &len);

    if (!content) {
        JS_FreeCString(ctx, path);
        JS_FreeCString(ctx, old_text);
        JS_FreeCString(ctx, new_text);
        return make_result_object(ctx, 0, "File not found", NULL, -1);
    }

    /* Check if old text exists */
    if (!strstr(content, old_text)) {
        free(content);
        JS_FreeCString(ctx, path);
        JS_FreeCString(ctx, old_text);
        JS_FreeCString(ctx, new_text);
        return make_result_object(ctx, 0, "Text not found in file", NULL, -1);
    }

    /* Perform replacement */
    size_t old_len = strlen(old_text);
    size_t new_len = strlen(new_text);

    /* Count occurrences */
    int count = 0;
    const char *p = content;
    while ((p = strstr(p, old_text)) != NULL) { count++; p += old_len; }

    /* Allocate result buffer */
    size_t new_size = len + (size_t)(count) * (new_len - old_len) + 1;
    char *result = (char *)malloc(new_size);
    if (!result) {
        free(content);
        JS_FreeCString(ctx, path);
        JS_FreeCString(ctx, old_text);
        JS_FreeCString(ctx, new_text);
        return make_result_object(ctx, 0, "Out of memory", NULL, -1);
    }

    /* Build result */
    char *out = result;
    const char *in = content;
    int replacements = 0;

    if (replace_all) {
        while (*in) {
            const char *found = strstr(in, old_text);
            if (found) {
                size_t before = (size_t)(found - in);
                memcpy(out, in, before);
                out += before;
                memcpy(out, new_text, new_len);
                out += new_len;
                in = found + old_len;
                replacements++;
            } else {
                strcpy(out, in);
                break;
            }
        }
    } else {
        /* Single replacement */
        const char *found = strstr(in, old_text);
        if (found) {
            size_t before = (size_t)(found - in);
            memcpy(out, in, before);
            out += before;
            memcpy(out, new_text, new_len);
            out += new_len;
            in = found + old_len;
            strcpy(out, in);
            replacements = 1;
        }
    }

    /* Write back */
    FILE *f = fopen(path, "wb");
    if (!f) {
        free(content);
        free(result);
        JS_FreeCString(ctx, path);
        JS_FreeCString(ctx, old_text);
        JS_FreeCString(ctx, new_text);
        return make_result_object(ctx, 0, "Cannot write file", NULL, -1);
    }
    fwrite(result, 1, strlen(result), f);
    fclose(f);

    free(content);
    free(result);
    JS_FreeCString(ctx, path);
    JS_FreeCString(ctx, old_text);
    JS_FreeCString(ctx, new_text);

    return make_result_object(ctx, 1, NULL, NULL, replacements);
}

/* ================================================================
 * aceGlob — Find files matching pattern
 * pal.aceGlob(pattern, basePath?) → { success, files, count, error }
 *
 * Simple glob matching: ** → match any path, * → match any non-/ chars
 * ================================================================ */

/* Convert glob pattern to regex-like matching */
static int glob_match(const char *pattern, const char *filename)
{
    const char *p = pattern, *f = filename;
    while (*p) {
        if (*p == '*' && *(p + 1) == '*') {
            p += 2;
            /* ** matches any path segment */
            while (*f && *f != '/' && !glob_match(p, f)) f++;
            if (!*f) return glob_match(p, f);
            if (*f == '/') f++;
        } else if (*p == '*') {
            p++;
            /* * matches any non-/ chars */
            while (*f && *f != '/' && !glob_match(p, f)) f++;
        } else if (*p == '?') {
            p++;
            if (!*f || *f == '/') return 0;
            f++;
        } else {
            if (*p != *f) return 0;
            p++; f++;
        }
    }
    return *f == '\0';
}

static int glob_walk(const char *base, const char *pattern,
                     char **results, int *count, int max_results)
{
    DIR *dir = opendir(base);
    if (!dir) return 0;

    struct dirent *entry;
    while ((entry = readdir(dir)) != NULL) {
        if (entry->d_name[0] == '.') continue;
        if (strcmp(entry->d_name, "node_modules") == 0) continue;

        char fullpath[ACE_TOOL_PATH_MAX];
        snprintf(fullpath, sizeof(fullpath), "%s/%s", base, entry->d_name);

        struct stat st;
        if (stat(fullpath, &st) != 0) continue;

        if (S_ISDIR(st.st_mode)) {
            glob_walk(fullpath, pattern, results, count, max_results);
        } else if (S_ISREG(st.st_mode)) {
            if (*count < max_results && glob_match(pattern, fullpath)) {
                results[*count] = strdup(fullpath);
                (*count)++;
            }
        }
    }
    closedir(dir);
    return 0;
}

static JSValue js_ace_glob(JSContext *ctx, JSValueConst this_val,
                           int argc, JSValueConst *argv)
{
    (void)this_val;
    if (argc < 1) return JS_ThrowTypeError(ctx, "aceGlob: pattern required");

    const char *pattern = JS_ToCString(ctx, argv[0]);
    const char *base = (argc >= 2) ? JS_ToCString(ctx, argv[1]) : ".";

    if (!pattern) return JS_ThrowTypeError(ctx, "aceGlob: pattern must be string");

    /* Allocate results array */
    int max = 1000;
    char **results = (char **)calloc((size_t)max, sizeof(char *));
    int count = 0;

    glob_walk(base, pattern, results, &count, max);

    /* Build JS array */
    JSValue arr = JS_NewArray(ctx);
    for (int i = 0; i < count; i++) {
        JS_SetPropertyInt64(ctx, arr, i, JS_NewString(ctx, results[i]));
        free(results[i]);
    }
    free(results);

    JS_FreeCString(ctx, pattern);
    if (argc >= 2) JS_FreeCString(ctx, base);

    JSValue obj = JS_NewObject(ctx);
    JS_SetPropertyStr(ctx, obj, "success", JS_NewBool(ctx, 1));
    JS_SetPropertyStr(ctx, obj, "files", arr);
    JS_SetPropertyStr(ctx, obj, "count", JS_NewInt32(ctx, count));
    return obj;
}

/* ================================================================
 * aceGrep — Search for pattern in files
 * pal.aceGrep(pattern, path) → { success, matches, count, error }
 * ================================================================ */

static JSValue js_ace_grep(JSContext *ctx, JSValueConst this_val,
                           int argc, JSValueConst *argv)
{
    (void)this_val;
    if (argc < 2) return JS_ThrowTypeError(ctx, "aceGrep: pattern and path required");

    const char *pattern_str = JS_ToCString(ctx, argv[0]);
    const char *search_path = JS_ToCString(ctx, argv[1]);
    if (!pattern_str || !search_path) {
        if (pattern_str) JS_FreeCString(ctx, pattern_str);
        if (search_path) JS_FreeCString(ctx, search_path);
        return JS_ThrowTypeError(ctx, "aceGrep: pattern and path must be strings");
    }

    /* Simple substring search (no regex for now) */
    JSValue arr = JS_NewArray(ctx);
    int match_count = 0;
    int max_matches = 100;

    struct stat st;
    if (stat(search_path, &st) != 0) {
        JS_FreeCString(ctx, pattern_str);
        JS_FreeCString(ctx, search_path);
        return make_result_object(ctx, 0, "Path not found", NULL, -1);
    }

    if (S_ISDIR(st.st_mode)) {
        /* Search directory recursively */
        DIR *dir = opendir(search_path);
        if (!dir) {
            JS_FreeCString(ctx, pattern_str);
            JS_FreeCString(ctx, search_path);
            return make_result_object(ctx, 0, "Cannot open directory", NULL, -1);
        }

        struct dirent *entry;
        /* Collect file paths first, then search each.
         * Heap-allocated to avoid blowing the stack (2000 × 2048 = 4 MB). */
        char (*filepaths)[ACE_TOOL_PATH_MAX] = calloc(2000, ACE_TOOL_PATH_MAX);
        if (!filepaths) {
            closedir(dir);
            JS_FreeCString(ctx, pattern_str);
            JS_FreeCString(ctx, search_path);
            return JS_ThrowOutOfMemory(ctx);
        }
        int fcount = 0;

        while ((entry = readdir(dir)) != NULL && fcount < 2000) {
            if (entry->d_name[0] == '.') continue;
            if (strcmp(entry->d_name, "node_modules") == 0) continue;

            snprintf(filepaths[fcount], ACE_TOOL_PATH_MAX, "%s/%s", search_path, entry->d_name);
            struct stat fst;
            if (stat(filepaths[fcount], &fst) == 0 && S_ISREG(fst.st_mode)) {
                fcount++;
            }
        }
        closedir(dir);

        for (int i = 0; i < fcount && match_count < max_matches; i++) {
            size_t len = 0;
            char *content = read_entire_file(filepaths[i], &len);
            if (!content) continue;

            const char *line_start = content;
            int line_num = 1;
            while (*line_start && match_count < max_matches) {
                const char *line_end = strchr(line_start, '\n');
                size_t line_len = line_end ? (size_t)(line_end - line_start) : strlen(line_start);

                if (memmem(line_start, line_len, pattern_str, strlen(pattern_str))) {
                    JSValue match_obj = JS_NewObject(ctx);
                    JS_SetPropertyStr(ctx, match_obj, "file", JS_NewString(ctx, filepaths[i]));
                    JS_SetPropertyStr(ctx, match_obj, "line", JS_NewInt32(ctx, line_num));
                    /* Trimmed line content */
                    char trimmed[256];
                    size_t tlen = line_len < 255 ? line_len : 255;
                    memcpy(trimmed, line_start, tlen);
                    trimmed[tlen] = '\0';
                    JS_SetPropertyStr(ctx, match_obj, "content", JS_NewString(ctx, trimmed));
                    JS_SetPropertyInt64(ctx, arr, match_count, match_obj);
                    match_count++;
                }

                line_start = line_end ? line_end + 1 : line_start + line_len;
                line_num++;
            }
            free(content);
        }
        free(filepaths);
    } else if (S_ISREG(st.st_mode)) {
        /* Search single file */
        size_t len = 0;
        char *content = read_entire_file(search_path, &len);
        if (!content) {
            JS_FreeCString(ctx, pattern_str);
            JS_FreeCString(ctx, search_path);
            return make_result_object(ctx, 0, "Cannot read file", NULL, -1);
        }

        const char *line_start = content;
        int line_num = 1;
        while (*line_start && match_count < max_matches) {
            const char *line_end = strchr(line_start, '\n');
            size_t line_len = line_end ? (size_t)(line_end - line_start) : strlen(line_start);

            if (memmem(line_start, line_len, pattern_str, strlen(pattern_str))) {
                JSValue match_obj = JS_NewObject(ctx);
                JS_SetPropertyStr(ctx, match_obj, "file", JS_NewString(ctx, search_path));
                JS_SetPropertyStr(ctx, match_obj, "line", JS_NewInt32(ctx, line_num));
                char trimmed[256];
                size_t tlen = line_len < 255 ? line_len : 255;
                memcpy(trimmed, line_start, tlen);
                trimmed[tlen] = '\0';
                JS_SetPropertyStr(ctx, match_obj, "content", JS_NewString(ctx, trimmed));
                JS_SetPropertyInt64(ctx, arr, match_count, match_obj);
                match_count++;
            }

            line_start = line_end ? line_end + 1 : line_start + line_len;
            line_num++;
        }
        free(content);
    }

    JS_FreeCString(ctx, pattern_str);
    JS_FreeCString(ctx, search_path);

    JSValue obj = JS_NewObject(ctx);
    JS_SetPropertyStr(ctx, obj, "success", JS_NewBool(ctx, 1));
    JS_SetPropertyStr(ctx, obj, "matches", arr);
    JS_SetPropertyStr(ctx, obj, "count", JS_NewInt32(ctx, match_count));
    return obj;
}

/* ================================================================
 * aceBash — Execute shell command
 * pal.aceBash(command, timeout?) → { success, stdout, stderr, exitCode, error }
 *
 * Uses popen() for simplicity. Not as robust as a full process
 * management system, but sufficient for agent tool use.
 * ================================================================ */

static JSValue js_ace_bash(JSContext *ctx, JSValueConst this_val,
                           int argc, JSValueConst *argv)
{
    (void)this_val;
    if (argc < 1) return JS_ThrowTypeError(ctx, "aceBash: command required");

    const char *command = JS_ToCString(ctx, argv[0]);
    if (!command) return JS_ThrowTypeError(ctx, "aceBash: command must be string");

    /* Execute via popen */
    FILE *pipe = popen(command, "r");
    JS_FreeCString(ctx, command);

    if (!pipe) {
        return make_result_object(ctx, 0, "Failed to execute command", NULL, -1);
    }

    /* Read stdout. Use heap buffer to avoid 4 KB stack allocation. */
    char *buf = (char *)malloc(ACE_TOOL_READ_BUF);
    if (!buf) return make_result_object(ctx, 0, "Out of memory", NULL, -1);
    size_t stdout_cap = 4096;
    size_t stdout_len = 0;
    char *stdout_buf = (char *)malloc(stdout_cap);
    if (!stdout_buf) { free(buf); return make_result_object(ctx, 0, "Out of memory", NULL, -1); }
    stdout_buf[0] = '\0';

    while (fgets(buf, ACE_TOOL_READ_BUF, pipe)) {
        size_t chunk_len = strlen(buf);
        if (stdout_len + chunk_len + 1 > stdout_cap) {
            stdout_cap *= 2;
            char *new_buf = (char *)realloc(stdout_buf, stdout_cap);
            if (!new_buf) { free(buf); free(stdout_buf); pclose(pipe); return make_result_object(ctx, 0, "Out of memory", NULL, -1); }
            stdout_buf = new_buf;
        }
        memcpy(stdout_buf + stdout_len, buf, chunk_len);
        stdout_len += chunk_len;
        stdout_buf[stdout_len] = '\0';
    }

    int exit_status = pclose(pipe);
    int exit_code = exit_status >> 8;  /* Extract exit code from wait status */

    JSValue obj = JS_NewObject(ctx);
    JS_SetPropertyStr(ctx, obj, "success", JS_NewBool(ctx, exit_code == 0));
    JS_SetPropertyStr(ctx, obj, "stdout", JS_NewString(ctx, stdout_buf));
    JS_SetPropertyStr(ctx, obj, "stderr", JS_NewString(ctx, "")); /* popen doesn't capture stderr */
    JS_SetPropertyStr(ctx, obj, "exitCode", JS_NewInt32(ctx, exit_code));
    if (exit_code != 0) {
        char err[128];
        snprintf(err, sizeof(err), "Command exited with code %d", exit_code);
        JS_SetPropertyStr(ctx, obj, "error", JS_NewString(ctx, err));
    }

    free(buf);
    free(stdout_buf);
    return obj;
}

/* ================================================================
 * aceExists — Check if file/directory exists
 * pal.aceExists(path) → true/false
 * ================================================================ */

static JSValue js_ace_exists(JSContext *ctx, JSValueConst this_val,
                             int argc, JSValueConst *argv)
{
    (void)this_val;
    if (argc < 1) return JS_ThrowTypeError(ctx, "aceExists: path required");

    const char *path = JS_ToCString(ctx, argv[0]);
    if (!path) return JS_NewBool(ctx, 0);

    struct stat st;
    int exists = (stat(path, &st) == 0);
    JS_FreeCString(ctx, path);
    return JS_NewBool(ctx, exists);
}

/* ================================================================
 * Extension init hook
 * ================================================================ */

static int ace_tools_ext_init(qwrt_ext_t *ext, qwrt_t *rt)
{
    JSContext *ctx = qwrt_get_jsctx(rt);
    if (!ctx) return -1;

    JSValue global = JS_GetGlobalObject(ctx);

    /* Find the pal object */
    JSValue pal = JS_GetPropertyStr(ctx, global, "__pal__");
    if (JS_IsUndefined(pal) || JS_IsException(pal)) {
        JS_FreeValue(ctx, pal);
        pal = JS_GetPropertyStr(ctx, global, "pal");
    }
    if (JS_IsUndefined(pal) || JS_IsException(pal)) {
        JS_FreeValue(ctx, pal);
        pal = JS_GetPropertyStr(ctx, global, "__pal_inject__");
    }
    if (JS_IsUndefined(pal) || JS_IsException(pal)) {
        JS_FreeValue(ctx, pal);
        JS_FreeValue(ctx, global);
        return -1;
    }

    /* Register all native tool functions */
    JS_SetPropertyStr(ctx, pal, "aceRead",
        JS_NewCFunction(ctx, js_ace_read, "aceRead", 1));
    JS_SetPropertyStr(ctx, pal, "aceWrite",
        JS_NewCFunction(ctx, js_ace_write, "aceWrite", 2));
    JS_SetPropertyStr(ctx, pal, "aceEdit",
        JS_NewCFunction(ctx, js_ace_edit, "aceEdit", 3));
    JS_SetPropertyStr(ctx, pal, "aceGlob",
        JS_NewCFunction(ctx, js_ace_glob, "aceGlob", 1));
    JS_SetPropertyStr(ctx, pal, "aceGrep",
        JS_NewCFunction(ctx, js_ace_grep, "aceGrep", 2));
    JS_SetPropertyStr(ctx, pal, "aceBash",
        JS_NewCFunction(ctx, js_ace_bash, "aceBash", 1));
    JS_SetPropertyStr(ctx, pal, "aceExists",
        JS_NewCFunction(ctx, js_ace_exists, "aceExists", 1));

    JS_FreeValue(ctx, pal);
    JS_FreeValue(ctx, global);

    (void)ext;
    return 0;
}

#endif /* QWRT_WITH_ACE_TOOLS */

/* ================================================================
 * Extension definition (always compiled, no-op when disabled)
 * ================================================================ */

const qwrt_ext_t qwrt_ace_tools_ext = {
    .name = "ace_tools",
    .version = 1,
#ifdef QWRT_WITH_ACE_TOOLS
    .init = ace_tools_ext_init,
    .destroy = NULL,
    .suspend = NULL,
    .resume = NULL,
#else
    .init = NULL,
    .destroy = NULL,
    .suspend = NULL,
    .resume = NULL,
#endif
    .user_data = NULL,
};