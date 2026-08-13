#include "mock_libuv.h"
#include <gtest/gtest.h>
#include <unistd.h>

/* The mock's callbacks receive only the handle (no user_data), so tests
 * observe side effects through test-file statics captured by the callbacks. */

static int g_timer_fired = 0;
static void on_timer(uv_timer_t *h) { (void)h; g_timer_fired++; }

static int g_async_fired = 0;
static void on_async(uv_async_t *h) { (void)h; g_async_fired++; }

TEST(mock_uv, timer_fires_after_timeout)
{
    uv_loop_t l;
    ASSERT_EQ(0, uv_loop_init(&l));
    uv_timer_t t;
    ASSERT_EQ(0, uv_timer_init(&l, &t));
    g_timer_fired = 0;
    ASSERT_EQ(0, uv_timer_start(&t, on_timer, 1, 0));
    usleep(5000);                     /* ensure timeout elapses */
    ASSERT_EQ(0, uv_run(&l, 1));      /* ONCE: fires due timer then returns */
    EXPECT_EQ(1, g_timer_fired);
    EXPECT_EQ(0, uv_loop_close(&l));  /* one-shot stopped itself */
}

TEST(mock_uv, nowait_returns_immediately)
{
    uv_loop_t l;
    ASSERT_EQ(0, uv_loop_init(&l));
    uv_timer_t t;
    ASSERT_EQ(0, uv_timer_init(&l, &t));
    g_timer_fired = 0;
    ASSERT_EQ(0, uv_timer_start(&t, on_timer, 100000, 0));  /* far future */
    ASSERT_EQ(0, uv_run(&l, 0));      /* NOWAIT: one pass, no blocking */
    EXPECT_EQ(0, g_timer_fired);
    uv_timer_stop(&t);
    ASSERT_EQ(0, uv_loop_close(&l));
}

TEST(mock_uv, async_send_fires_in_run)
{
    uv_loop_t l;
    ASSERT_EQ(0, uv_loop_init(&l));
    uv_async_t a;
    ASSERT_EQ(0, uv_async_init(&l, &a, on_async));
    g_async_fired = 0;
    ASSERT_EQ(0, uv_async_send(&a));
    ASSERT_EQ(0, uv_run(&l, 1));      /* ONCE: fires pending async then returns */
    EXPECT_EQ(1, g_async_fired);
    /* async stays active until closed — drain it before loop_close */
    uv_close((uv_handle_t *)&a, NULL);
    ASSERT_EQ(0, uv_run(&l, 0));
    ASSERT_EQ(0, uv_loop_close(&l));
}

static uv_loop_t *g_thread_loop = NULL;
static int g_run_started = 0;
static int g_run_done = 0;

static void on_async_wake(uv_async_t *h)
{
    (void)h;
    g_async_fired = 1;
    uv_stop(g_thread_loop);           /* terminate the blocked uv_run */
}

static void run_once_thread(void *arg)
{
    uv_loop_t *l = (uv_loop_t *)arg;
    g_run_started = 1;
    uv_run(l, 1);
    g_run_done = 1;
}

TEST(mock_uv, async_send_wakes_blocked_run)
{
    uv_loop_t l;
    ASSERT_EQ(0, uv_loop_init(&l));
    uv_async_t a;
    ASSERT_EQ(0, uv_async_init(&l, &a, on_async_wake));
    g_async_fired = g_run_started = g_run_done = 0;
    g_thread_loop = &l;

    uv_thread_t thr;
    ASSERT_EQ(0, uv_thread_create(&thr, run_once_thread, &l));
    while (!g_run_started) usleep(100);        /* wait until run is blocking */
    usleep(20000);
    ASSERT_EQ(0, uv_async_send(&a));
    ASSERT_EQ(0, uv_thread_join(&thr));
    EXPECT_EQ(1, g_async_fired);
    EXPECT_EQ(1, g_run_done);

    uv_close((uv_handle_t *)&a, NULL);
    ASSERT_EQ(0, uv_run(&l, 0));
    ASSERT_EQ(0, uv_loop_close(&l));
}

static int g_close_called = 0;
static void on_close(uv_handle_t *h) { (void)h; g_close_called++; }

TEST(mock_uv, close_runs_callback_and_drops_active)
{
    uv_loop_t l;
    ASSERT_EQ(0, uv_loop_init(&l));
    uv_timer_t t;
    ASSERT_EQ(0, uv_timer_init(&l, &t));
    ASSERT_EQ(0, uv_timer_start(&t, on_timer, 100000, 0));
    ASSERT_EQ(-1, uv_loop_close(&l));           /* EBUSY: timer still active */

    g_close_called = 0;
    uv_close((uv_handle_t *)&t, on_close);
    ASSERT_EQ(0, uv_run(&l, 0));
    EXPECT_EQ(1, g_close_called);
    ASSERT_EQ(0, uv_loop_close(&l));
}

TEST(mock_uv, walk_visits_handles)
{
    uv_loop_t l;
    ASSERT_EQ(0, uv_loop_init(&l));
    uv_timer_t t;
    uv_async_t a;
    ASSERT_EQ(0, uv_timer_init(&l, &t));
    ASSERT_EQ(0, uv_async_init(&l, &a, on_async));
    int seen = 0;
    uv_walk(&l, [](uv_handle_t *h, void *arg) { (void)h; (*(int *)arg)++; }, &seen);
    EXPECT_EQ(2, seen);
    uv_close((uv_handle_t *)&a, NULL);
    uv_close((uv_handle_t *)&t, NULL);
    ASSERT_EQ(0, uv_run(&l, 0));
    ASSERT_EQ(0, uv_loop_close(&l));
}

TEST(mock_uv, timer_repeat_stays_active)
{
    uv_loop_t l;
    ASSERT_EQ(0, uv_loop_init(&l));
    uv_timer_t t;
    ASSERT_EQ(0, uv_timer_init(&l, &t));
    g_timer_fired = 0;
    ASSERT_EQ(0, uv_timer_start(&t, on_timer, 1, 50));
    usleep(5000);
    ASSERT_EQ(0, uv_run(&l, 1));
    EXPECT_EQ(1, g_timer_fired);
    EXPECT_EQ(-1, uv_loop_close(&l));           /* repeat timer still active */
    uv_timer_stop(&t);
    ASSERT_EQ(0, uv_loop_close(&l));
}

static uv_mutex_t g_m;
static uv_cond_t g_c;
static int g_cond_flag = 0;
static int g_cond_seen = 0;

static void cond_wait_thread(void *arg)
{
    (void)arg;
    uv_mutex_lock(&g_m);
    while (!g_cond_flag) uv_cond_wait(&g_c, &g_m);
    uv_mutex_unlock(&g_m);
    g_cond_seen = 1;
}

TEST(mock_uv, mutex_cond_roundtrip)
{
    ASSERT_EQ(0, uv_mutex_init(&g_m));
    ASSERT_EQ(0, uv_cond_init(&g_c));
    g_cond_flag = g_cond_seen = 0;
    uv_thread_t thr;
    ASSERT_EQ(0, uv_thread_create(&thr, cond_wait_thread, NULL));
    usleep(20000);
    uv_mutex_lock(&g_m);
    g_cond_flag = 1;
    uv_cond_signal(&g_c);
    uv_mutex_unlock(&g_m);
    ASSERT_EQ(0, uv_thread_join(&thr));
    EXPECT_EQ(1, g_cond_seen);
    uv_cond_destroy(&g_c);
    uv_mutex_destroy(&g_m);
}

static uv_thread_t *g_self_thr = NULL;
static int g_self_equal = 0;

static void self_check_thread(void *arg)
{
    (void)arg;
    g_self_equal = uv_thread_equal(g_self_thr, g_self_thr);
}

TEST(mock_uv, thread_self_and_equal)
{
    uv_thread_t main_thr = uv_thread_self();
    uv_thread_t other;
    g_self_thr = &main_thr;
    g_self_equal = 0;
    ASSERT_EQ(0, uv_thread_create(&other, self_check_thread, NULL));
    ASSERT_EQ(0, uv_thread_join(&other));
    EXPECT_EQ(1, g_self_equal);
    EXPECT_FALSE(uv_thread_equal(&main_thr, &other));
}
