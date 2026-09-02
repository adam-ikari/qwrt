/**
 * qwrt polyfill: the gRPC/HTTP2 stack, as one swappable wiring unit.
 *
 * `index.js` imports this through the virtual specifier `@qwrt/grpc-stack`,
 * which polyfill/build.js aliases here when QWRT_WITH_GRPC=1 and to
 * grpc-stack-stub.js when it is 0. Aliasing (rather than a runtime `if`) is
 * what actually keeps http2/hpack/protobuf/flatbuffers/grpc out of the default
 * bundle: esbuild does not delete `if (false)` bodies unless it minifies, so a
 * guarded call would still ship ~3.5k lines.
 *
 * Serialization policy (see grpc.js for the rationale):
 *   - protobuf is the default codec on every call, so a qwrt client talks to
 *     any standard gRPC peer (grpc-go, grpc-js, grpcurl, Envoy).
 *   - flatbuffers is an opt-in internal fast path, selected per call with
 *     {serialization:'flatbuffers'} and only viable when both ends are qwrt.
 */

import { pal } from './pal.js';
import { setupHttp2 } from './http2.js';
import { setupProtobuf } from './protobuf.js';
import { setupFlatbuffers } from './flatbuffers.js';
import { setupGrpc } from './grpc.js';

export function setupGrpcStack() {
  setupHttp2(pal);
  setupProtobuf(pal);
  setupFlatbuffers(pal);
  setupGrpc(pal);
}
