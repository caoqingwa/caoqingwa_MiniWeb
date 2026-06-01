#pragma once
#include <cstddef>
#include <string>
#include <vector>
#include <algorithm>
#include <cstring>


class Buffer {
public:
    explicit Buffer(size_t initial_size = k_initial_size);

    size_t readable_bytes() const;
    size_t writable_bytes() const;
    size_t prependable_bytes() const;

    const char* peek() const;

    void retrieve(size_t len);
    void retrieve_until(const char* end);
    void retrieve_all();
    std::string retrieve_all_to_str();

    static constexpr size_t kMaxSize = 2 * 1024 * 1024;

    bool ensure_writable(size_t len);
    char* begin_write();
    const char* begin_write_const() const;
    void has_written(size_t len);

    bool append(const std::string& str);
    bool append(const char* data, size_t len);
    bool append(const void* data, size_t len);

private:
    char* begin();
    const char* begin() const;
    bool make_space(size_t len);

    static const size_t k_initial_size = 1024;
    std::vector<char> buffer_;
    size_t read_pos_;
    size_t write_pos_;
};
