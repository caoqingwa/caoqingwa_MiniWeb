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

bool Buffer::ensure_writable(size_t len) {
    if (writable_bytes() < len) {
        return make_space(len);
    }
    return true;
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

bool Buffer::append(const std::string& str) {
    return append(str.data(), str.size());
}

bool Buffer::append(const char* data, size_t len) {
    if (data == nullptr || len == 0) {
        return true;
    }

    if (!ensure_writable(len)) {
        return false;
    }
    std::memcpy(begin_write(), data, len);
    has_written(len);
    return true;
}

bool Buffer::append(const void* data, size_t len) {
    return append(static_cast<const char*>(data), len);
}

char* Buffer::begin() {
    return buffer_.data();
}

const char* Buffer::begin() const {
    return buffer_.data();
}

bool Buffer::make_space(size_t len) {
    if (writable_bytes() + prependable_bytes() < len) {
        const size_t new_size = write_pos_ + len;
        if (new_size > kMaxSize) {
            return false;
        }
        buffer_.resize(new_size);
    } else {
        const size_t readable = readable_bytes();
        std::copy(begin() + read_pos_, begin() + write_pos_, begin());
        read_pos_ = 0;
        write_pos_ = readable;
    }
    return true;
}