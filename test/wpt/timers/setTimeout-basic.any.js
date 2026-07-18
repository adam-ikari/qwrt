// META: global=shell

test(() => {
  assert_equals(typeof setTimeout, 'function');
  assert_equals(typeof clearTimeout, 'function');
  assert_equals(typeof setInterval, 'function');
  assert_equals(typeof clearInterval, 'function');
}, 'timer functions exist');

promise_test(async (t) => {
  const start = Date.now();
  await new Promise(resolve => setTimeout(resolve, 50));
  const elapsed = Date.now() - start;
  assert_greater_than_equal(elapsed, 40, 'setTimeout fired in reasonable time');
}, 'setTimeout with delay');

promise_test(async (t) => {
  let fired = false;
  const id = setTimeout(() => { fired = true; }, 100);
  clearTimeout(id);
  await new Promise(resolve => setTimeout(resolve, 150));
  assert_false(fired, 'clearTimeout prevented callback');
}, 'clearTimeout cancels callback');
