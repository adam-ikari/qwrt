// META: global=shell

test(() => {
  assert_equals(typeof structuredClone, 'function');
}, 'structuredClone exists');

test(() => {
  const obj = { a: 1, b: [2, 3], c: { d: 4 } };
  const cloned = structuredClone(obj);
  assert_not_equals(cloned, obj, 'returns different object');
  assert_equals(cloned.a, 1);
  assert_array_equals(cloned.b, [2, 3]);
  assert_equals(cloned.c.d, 4);
  cloned.a = 99;
  assert_equals(obj.a, 1, 'original not affected');
}, 'structuredClone deep clones objects');
