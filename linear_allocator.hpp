#pragma once
#include <cstddef>
#include <cstdint>
#include <cstring>

// A simple linear allocator for embedded systems.
// It allocates memory linearly from a pre-allocated buffer and supports rewinding to a previous mark.
// This is useful for scenarios where you want to allocate temporary memory and then free it all at once by rewinding to a previous state.
class LinearAllocator {
public:
    // Initializes the allocator with a given buffer and size
    void init(uint8_t* buffer, size_t size) {
        base_ = buffer;
        capacity_ = size;
        offset_ = 0;
        high_water_ = 0;
        generation_ = 0;
        memset(base_, 0xDD, capacity_);
    }

    // A mark represents a point in the allocation history to which we can rewind
    struct Mark {
        size_t offset;
        uint32_t generation;
    };

    // Allocates a block of memory of the given size and alignment
    void* allocate(size_t size, size_t alignment = 4) { // 4-byte alignment by default
        size_t start = alignup(offset_, alignment);
        if (start + size > capacity_) {
            return nullptr; // Not enough space
        }
        void* ptr = base_ + start;
        offset_ = start + size;
        if (offset_ > high_water_) {
            high_water_ = offset_;
        }
        return ptr;
    }

    // Returns a mark that can be used to rewind the allocator to this point
    Mark mark() const {
        return { offset_, generation_ };
    }

    // Rewinds the allocator to a previous mark, effectively freeing all allocations made after that mark
    bool rewind(Mark mark) {
        if (mark.generation != generation_ || mark.offset > offset_) {
            return false; // Mark is invalid
        }
        memset(base_ + mark.offset, 0xDD, offset_ - mark.offset);
        offset_ = mark.offset;
        return true;
    }

    // Resets the allocator, effectively freeing all allocated memory
    void reset() {
        memset(base_, 0xDD, capacity_);
        offset_ = 0;
        generation_++;
    }

    size_t used() const       { return offset_; }
    size_t freeSpace() const  { return capacity_ - offset_; }
    size_t capacity() const   { return capacity_; }
    size_t highWater() const  { return high_water_; }

private:
    // Aligns the given value up to the nearest multiple of the specified alignment
    size_t alignup(size_t value, size_t alignment) const {
        return (value + alignment - 1) & ~(alignment - 1); 
    }

    uint8_t* base_ = nullptr; // Pointer to the start of the allocated buffer
    size_t capacity_ = 0; // Total size of the allocated buffer
    size_t offset_ = 0; // Current offset in the buffer where the next allocation will occur
    size_t high_water_ = 0; // Tracks the maximum offset reached, useful for monitoring memory usage
    uint32_t generation_ = 0; // Generation counter to track the validity of marks
};