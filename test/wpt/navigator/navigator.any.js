// META: global=shell

test(() => {
  assert_equals(typeof navigator, 'object');
}, 'navigator exists');

test(() => {
  assert_equals(typeof navigator.userAgent, 'string');
  assert_greater_than(navigator.userAgent.length, 0);
}, 'navigator.userAgent is non-empty string');
