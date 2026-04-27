#include "buffer.h"

Buffer::Buffer(size_t initial_size)
    : buffer_(initial_size), read_pos_(0), write_pos_(0) {
}

size_t Buffer::readable_bytes() const {
    return write_pos_ - read_pos_;
}

size_t Buffer::writable_bytes() const {
    return buffer_.size() - write_pos_;
}

size_t Buffer::prependable_bytes() const {
    return read_pos_;
}

const char* Buffer::peek() const {
    return begin() + read_pos_;
}

void Buffer::retrieve(size_t len) {
    if (len < readable_bytes()) {
        read_pos_ += len;
    }
    else {
        retrieve_all();
    }
}

void Buffer::retrieve_until(const char* end) {
    if (end >= peek() && end <= begin_write_const()) {
        retrieve(static_cast<size_t>(end - peek()));
    }
}

void Buffer::retrieve_all() {
    read_pos_ = 0;
    write_pos_ = 0;
}

std::string Buffer::retrieve_all_to_str() {
    std::string str(peek(), readable_bytes());
    retrieve_all();
    return str;
}

void Buffer::ensure_writable(size_t len) {
    if (writable_bytes() < len) {
        make_space(len);
    }
}

char* Buffer::begin_write() {
    return begin() + write_pos_;
}

const char* Buffer::begin_write_const() const {
    return begin() + write_pos_;
}

void Buffer::has_written(size_t len) {
    write_pos_ += len;
}

void Buffer::append(const std::string& str) {
    append(str.data(), str.size());
}

void Buffer::append(const char* data, size_t len) {
    if (data == nullptr || len == 0) {
        return;
    }

    ensure_writable(len);
    std::memcpy(begin_write(), data, len);
    has_written(len);
}

void Buffer::append(const void* data, size_t len) {
    append(static_cast<const char*>(data), len);
}

char* Buffer::begin() {
    return buffer_.data();
}

const char* Buffer::begin() const {
    return buffer_.data();
}

void Buffer::make_space(size_t len) {
    if (writable_bytes() + prependable_bytes() < len) {
        buffer_.resize(write_pos_ + len);
        return;
    }

    const size_t readable = readable_bytes();
    std::copy(begin() + read_pos_, begin() + write_pos_, begin());
    read_pos_ = 0;
    write_pos_ = readable;
}