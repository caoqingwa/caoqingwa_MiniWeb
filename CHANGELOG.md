# Changelog

## [Unreleased]

### Fixed
- **Race condition in SelectLoop**: `write_all` in thread pool lambda could race with `close_client` on the main thread. Added socket mutex protection around write operations in the thread pool path.
- **CMakeLists.txt redundant sources**: Removed explicit `buffer.cpp` / `http_handler.cpp` listings that were already covered by `GLOB CORE_SRC`.
- **Low listen backlog**: Changed `listen()` backlog from hardcoded `32` to `SOMAXCONN` on both Windows and Linux.

### Changed
- **Larger recv buffer**: Increased socket read buffer from 1KB to 64KB to reduce syscall frequency under load.
- **Shared utility header**: Extracted duplicated `to_lower()` (previously defined in 3 files) into `include/util.h`.
- **HTTP response builder**: Extracted repeated 200/400/404/413 response formatting into `HttpHandler::build_status_response()` helper, removing ~60 lines of duplication.
- **Modern memory management**: Replaced raw `new`/`delete` for `EventLoop` with `std::unique_ptr` in `main.cpp`.
- **Optimized MIME detection**: `get_content_type()` now only lowercases the file extension instead of the entire path.
- **Support HEAD method**: `HEAD` requests now return the same headers as `GET` but with an empty body.

### Added
- **URL decoding**: Added percent-decoding (`%XX`) for request paths, supporting spaces, Chinese characters, and other encoded values.
- **`include/util.h`**: Shared `to_lower()` and `url_decode()` utility functions.
