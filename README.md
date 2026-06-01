# caoqingwa_MiniWeb

A minimal HTTP web server written in C++17, built for learning network programming and concurrent server design.

## Features

- **Cross-platform**: Windows (IOCP/select) and Linux (epoll) via platform abstraction layer
- **Thread pool**: Concurrent request handling with configurable worker threads
- **Ring buffer**: Efficient socket I/O with prependable/readable/writable regions
- **Static file serving**: Serves HTML, CSS, JS from configurable root directories
- **HTTP/1.1**: Request parsing, file routing, MIME type detection
- **Size limits**: Configurable buffer cap (2MB default), header/body limits to prevent memory exhaustion
- **Connection timeout**: Idle connection cleanup via timer manager

## Build

### Windows (MSVC)

```bash
cd caoqingwa_MiniWeb
mkdir build && cd build
cmake ..
cmake --build . --config Release
```

### Linux (GCC/Clang)

```bash
cd caoqingwa_MiniWeb
mkdir build && cd build
cmake ..
make
```

## Usage

```bash
./server [port] [thread_count]
```

Defaults: port `8080`, 4 worker threads.

Static files are served from the `http/` directory relative to the executable.

## Architecture

```
platform/          Event loop per platform
  win/             select_loop.cpp
  linux/           epoll_loop.cpp
src/               Core logic
  buffer.cpp       Ring buffer (2MB cap)
  http_conn.cpp    HTTP request parser
  http_handler.cpp Response builder + static file serving
include/           Headers
  buffer.h, http_conn.h, http_handler.h,
  threadpool.h, timer.h, event_loop.h
http/              Static files served by the server
```

## License

MIT
