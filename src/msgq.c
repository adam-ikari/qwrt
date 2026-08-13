/*
 * qwrt Inbound Message Queue
 *
 * Thread-safe FIFO for host/worker → qwrt messages. Host threads call
 * qwrt_msg_push (via qwrt_post_message); the qwrt thread drains with
 * qwrt_msg_pop inside the wake callback. Each push also sends the wake
 * async so a blocked uv_run returns and dispatches.
 */

#include "qwrt_internal.h"
#include <stdlib.h>
#include <string.h>

int qwrt_msg_push(qwrt_t *rt, const char *data, size_t len, int source)
{
    qwrt_msg_t *m = (qwrt_msg_t *)malloc(sizeof *m + len + 1);
    if (!m) return -1;
    m->data = (char *)(m + 1);
    memcpy(m->data, data, len);
    m->data[len] = '\0';
    m->len = len;
    m->source = source;
    m->next = NULL;

    uv_mutex_lock(&rt->msg_mutex);
    if (rt->msg_tail) {
        rt->msg_tail->next = m;
    } else {
        rt->msg_head = m;
    }
    rt->msg_tail = m;
    uv_mutex_unlock(&rt->msg_mutex);

    uv_async_send(&rt->wake);   /* 唤醒阻塞中的 uv_run */
    return 0;
}

qwrt_msg_t *qwrt_msg_pop(qwrt_t *rt)
{
    uv_mutex_lock(&rt->msg_mutex);
    qwrt_msg_t *m = rt->msg_head;
    if (m) {
        rt->msg_head = m->next;
        if (!rt->msg_head) rt->msg_tail = NULL;
    }
    uv_mutex_unlock(&rt->msg_mutex);
    return m;
}

void qwrt_msg_free(qwrt_msg_t *m)
{
    free(m);
}
