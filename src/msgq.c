/*
 * qzjs Inbound Message Queue — lock-free MPSC on libuv primitives
 *
 * Philosophy: every qzjs runtime/worker is single-threaded; no mutex/cond
 * inside the core (on some kernels futex/pthread_cond wakeups are unreliable,
 * e.g. PVE 6.17 after an fd is created). Cross-thread communication uses only
 * libuv: uv_async_send for the wakeup, libuv's uv__queue for the container,
 * and GCC/Clang atomics for the memory ordering:
 *
 *   - push (host thread / worker thread): write the node, atomically exchange
 *     msg_tail to claim a unique predecessor, then release-store the link
 *     into the predecessor's q.next. Each producer gets a distinct predecessor,
 *     so there is no write-write race.
 *   - pop / has_pending (qzjs thread, exclusive consumer): acquire-load
 *     head->q.next. The consumer owns msg_head, so no lock is needed.
 *
 * Ordering: the ACQ_REL exchange pairs with the acquire load of q.next, making
 * the node contents written before the exchange visible to the consumer; FIFO
 * order follows the linked q.next chain.
 */

#include "qz_internal.h"
#include <stdlib.h>
#include <string.h>

int qz_msg_push(qz_t *rt, const char *data, size_t len, int source, int flags)
{
    /* len+1 与 sizeof(qz_msg_t) 相加不得溢出,否则 malloc 得到小块而
     * memcpy 越界写。len 来自 JS 侧 ArrayBuffer 长度或宿主,理论上可达
     * SIZE_MAX(整数溢出)或极大值(分配炸弹),统一在此拒绝。 */
    if (len > (size_t)-1 - sizeof(qz_msg_t) - 1) return -1;
    qz_msg_t *m = (qz_msg_t *)malloc(sizeof *m + len + 1);
    if (!m) return -1;   /* OOM:拒绝入队而非解引用 NULL */
    m->data = (char *)(m + 1);
    memcpy(m->data, data, len);
    m->data[len] = '\0';
    m->len = len;
    m->source = source;
    m->flags = (uint8_t)flags;

    /* Publish the node, then link it after the previous tail (ACQ_REL so the
     * consumer's acquire sees all fields written before the exchange). */
    __atomic_store_n(&m->q.next, (struct uv__queue *)NULL, __ATOMIC_RELAXED);
    qz_msg_t *prev = __atomic_exchange_n(&rt->msg_tail, m, __ATOMIC_ACQ_REL);
    __atomic_store_n(&prev->q.next, &m->q, __ATOMIC_RELEASE);

    uv_async_send(&rt->wake);   /* wake a blocked uv_run so it drains */
    return 0;
}

qz_msg_t *qz_msg_pop(qz_t *rt)
{
    /* Consumer-only (qzjs thread). Release strategy: msg_head always points at
     * the last returned node (alive, not freed); pop returns head->q.next and
     * advances head, freeing the OLD head (fully detached, no longer
     * referenced). The returned node is dispatched by the caller and becomes
     * the next pop's head — head never dangles. */
    qz_msg_t *head = rt->msg_head;
    struct uv__queue *nq = __atomic_load_n(&head->q.next, __ATOMIC_ACQUIRE);
    if (nq == NULL) return NULL;
    qz_msg_t *next = uv__queue_data(nq, qz_msg_t, q);
    rt->msg_head = next;
    if (head != &rt->msg_stub) free(head);
    return next;
}

int qz_msg_has_pending(qz_t *rt)
{
    /* Consumer-thread check; head does not migrate across threads, safe
     * without a lock. */
    return __atomic_load_n(&rt->msg_head->q.next, __ATOMIC_ACQUIRE) != NULL;
}

void qz_msg_free(qz_msg_t *m)
{
    free(m);
}
