/**
 * qwrt polyfill: the gRPC/HTTP2 stack, as one swappable wiring unit.
 *
 * `index.js` imports this through the virtual specifier `@qwrt/grpc-stack`,
 * which polyfill/build.js aliases here when QWRT_WITH_GRPC=1 and to
 * grpc-stack-stub.js when it is 0. Aliasing (rather than a runtime `if`) is
 * what actually keeps http2/hpack/protobuf/grpc out of the default
 * bundle: esbuild does not delete `if (false)` bodies unless it minifies, so a
 * guarded call would still ship ~3.5k lines.
 *
 * Serialization policy: protobuf is the only codec, so a qwrt client talks to
 * any standard gRPC peer (grpc-go, grpc-js, grpcurl, Envoy). Flatbuffers was
 * retired from the JS layer (see grpc.js header / ROADMAP H5).
 */

import { pal } from './pal.js';
import { setupHttp2 } from './http2.js';
import { setupProtobuf } from './protobuf.js';
import { setupGrpc } from './grpc.js';

export function setupGrpcStack() {
  setupHttp2(pal);
  setupProtobuf(pal);
  setupGrpc(pal);
}

