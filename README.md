# qwrt — QuickJS Runtime for ACE

qwrt is a lightweight QuickJS-ng runtime wrapper with a Platform Abstraction
Layer (PAL) for embedding JavaScript in C applications. It is the runtime
engine used by [ACE Core](https://github.com/ace-qwrt/ace-core).

## Features
- QuickJS-ng JavaScript engine
- PAL interface (libuv, FreeRTOS, mock)
- Streaming HTTP with TLS (mbedTLS)
- Compression (miniz), crypto, text codec extensions
- Multi-context support

## Build
```bash
cmake -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build -j$(nproc)
```

## License
MIT
