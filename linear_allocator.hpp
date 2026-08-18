#pragma once
#include <cstddef>
#include <cstdint>
#include <cstring>

class LinearAllocator {
public:
    void init(uint8_t* buffer, size_t size) {
        base_ = buffer;
        capacity_ = size;
        offset_ = 0;
        generation_ = 0;
        memset(base_, 0xDD, capacity_);
    }

    struct Handle {
        size_t offset;
        uint32_t generation;
    };

    void* allocate(size_t size, size_t alignment = 4) {
        size_t start = alignup(offset_, alignment);
        if (start + size > capacity_) {
            return nullptr; // Not enough space
        }
        void* ptr = base_ + start;
        offset_ = start + size;
        return ptr;
    }

    Handle handle() const {
        return { offset_, generation_ };
    }

    bool rewind(Handle handle) {
        if (handle.generation != generation_ || handle.offset > offset_) {
            return false; // Handle is invalid
        }
        memset(base_ + handle.offset, 0xDD, offset_ - handle.offset);
        offset_ = handle.offset;
        return true;
    }

    void reset() {
        memset(base_, 0xDD, capacity_);
        offset_ = 0;
        generation_++;
    }

    size_t freeSpace() const {
        return capacity_ - offset_;
    }

private:
    size_t alignup(size_t value, size_t alignment) const {
        return (value + alignment - 1) & ~(alignment - 1);
    }

    uint8_t* base_ = nullptr;
    size_t capacity_ = 0;
    size_t offset_ = 0;
    uint32_t generation_ = 0;
};