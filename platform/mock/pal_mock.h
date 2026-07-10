#ifndef QWRT_PAL_MOCK_H
#define QWRT_PAL_MOCK_H

#include "qwrt/qwrt.h"
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/* Create a mock PAL instance for testing */
qwrt_pal_t *pal_mock_create(void);

/* Destroy a mock PAL instance and free all mock data */
void pal_mock_destroy(qwrt_pal_t *pal);

/* Create a mock PAL that denies filesystem write and remove */
qwrt_pal_t *pal_mock_create_readonly_fs(void);

/* Create a mock PAL that denies HTTP requests */
qwrt_pal_t *pal_mock_create_no_http(void);

/* Set the mock time value returned by time_now (defaults to 0) */
void pal_mock_set_time(qwrt_pal_t *pal, uint64_t ms);

/* Get the current mock time value */
uint64_t pal_mock_get_time(qwrt_pal_t *pal);

/* Fire a specific timer by handle id */
void pal_mock_fire_timer(qwrt_pal_t *pal, int handle_id);

/* Fire all registered timers */
void pal_mock_fire_all_timers(qwrt_pal_t *pal);

/* Get captured log messages. Returns pointer to internal array of messages.
   *count is set to the number of log entries. Each entry is a null-terminated
   string prefixed with the level number and a colon, e.g. "1:hello world".
   The returned pointer is valid until pal_mock_destroy or pal_mock_clear_log. */
const char *pal_mock_get_log(qwrt_pal_t *pal, int *count);

/* Clear all captured log messages */
void pal_mock_clear_log(qwrt_pal_t *pal);

/* Override the default mock HTTP response (JSON string) */
void pal_mock_set_http_response(qwrt_pal_t *pal, const char *json_response);

/* Get the number of outstanding allocations (alloc count minus free count) */
int pal_mock_get_alloc_count(qwrt_pal_t *pal);

#ifdef __cplusplus
}
#endif

#endif /* QWRT_PAL_MOCK_H */
