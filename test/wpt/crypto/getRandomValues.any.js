// META: global=shell

test(() => {
  assert_equals(typeof crypto, 'object');
  assert_equals(typeof crypto.getRandomValues, 'function');
}, 'crypto.getRandomValues exists');

test(() => {
  const arr = new Uint8Array(16);
  crypto.getRandomValues(arr);
  let allZero = true;
  for (let i = 0; i < arr.length; i++) {
    if (arr[i] !== 0) { allZero = false; break; }
  }
  assert_false(allZero, 'random bytes are not all zero');
}, 'crypto.getRandomValues fills with random data');
