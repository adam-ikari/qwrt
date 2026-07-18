// META: global=shell

test(() => {
  assert_equals(typeof performance, 'object');
  assert_equals(typeof performance.now, 'function');
}, 'performance.now exists');

test(() => {
  const t1 = performance.now();
  const t2 = performance.now();
  assert_greater_than_equal(t2, t1, 'performance.now is monotonic');
}, 'performance.now returns monotonic numbers');
