#include <cstdio>
#include "pico/stdlib.h"
#include "pico/time.h"
#include "linear_allocator.hpp"
#include "uart_log.hpp"

static uint LED_PIN;
static UartLog uart_log;

static constexpr size_t ARENA_SIZE = 4096;
static uint8_t arena[ARENA_SIZE]; // Pre-allocated buffer for the linear allocator

// blink patterns (count, on_ms, off_ms)
struct BlinkPattern {
    int count;
    int on_ms;
    int off_ms;
};
static constexpr BlinkPattern PATTERN_OK        = { 1, 500, 200};  // Single long blink
static constexpr BlinkPattern PATTERN_REWIND    = { 2, 300, 200};  // Two medium blinks
static constexpr BlinkPattern PATTERN_FAIL      = { 3, 100, 100};  // Three short blinks
static constexpr BlinkPattern PATTERN_REJECTED  = { 4, 100, 100};  // Four very short blinks
static constexpr BlinkPattern PATTERN_RESET     = { 1, 1000, 200}; // Single long blink

// what to report for one branch of an operation's result
struct Outcome {
    const char*  msg;
    LogSeverity  sev;
    BlinkPattern pattern;
};

struct Point { uint32_t x, y; };

void blink(BlinkPattern pattern) {
    for (int i = 0; i < pattern.count; i++) {
        gpio_put(LED_PIN, 1);
        sleep_ms(pattern.on_ms);
        gpio_put(LED_PIN, 0);
        sleep_ms(pattern.off_ms);
    }
}

// Logs an event with the current state of the allocator and execution time
void log_event(LogSeverity severity, const LinearAllocator& allocator, uint64_t exec_us, const char* action) {
    char buf[220]; // Buffer to hold the log message
    snprintf(buf, sizeof(buf), "%s | used=%u free=%u high=%u cap=%u | %llu us",
            action, (unsigned int)allocator.used(), (unsigned int)allocator.freeSpace(),
            (unsigned int)allocator.highWater(), (unsigned int)allocator.capacity(),
            (unsigned long long)exec_us);
    uart_log.log(severity, buf);
}

void report (const LinearAllocator& allocator, uint64_t exec_us, const Outcome& outcome) {
    log_event(outcome.sev, allocator, exec_us, outcome.msg);
    blink(outcome.pattern);
    sleep_ms(1000);
}

// Allocation success always means the same thing, so the outcomes are fixed here
void* try_allocate(LinearAllocator& allocator, size_t size, const char* ok_msg, const char* fail_msg) {
    uint64_t t0 = time_us_64();
    void* ptr = allocator.allocate(size);
    uint64_t dt = time_us_64() - t0;
   
    report(allocator, dt, ptr ? Outcome{ok_msg, LogSeverity::INFO, PATTERN_OK} 
                              : Outcome{fail_msg, LogSeverity::ERROR, PATTERN_FAIL});

    return ptr;
}

// Rewind outcomes are caller-supplied: for a stale mark, success is the failure case, and failure is the expected outcome
bool try_rewind(LinearAllocator& allocator, LinearAllocator::Mark mark, 
                const Outcome& success_outcome, const Outcome& fail_outcome) {
    uint64_t t0 = time_us_64();
    bool success = allocator.rewind(mark);
    uint64_t dt = time_us_64() - t0;

    report(allocator, dt, success ? success_outcome : fail_outcome);
    return success;
}

void do_reset(LinearAllocator& allocator) {
    uint64_t t0 = time_us_64();
    allocator.reset();
    uint64_t dt = time_us_64() - t0;

    report(allocator, dt, Outcome{"Allocator reset", LogSeverity::INFO, PATTERN_RESET});
}

// array of 64 uint32_t, written and read back to prove the whole extent is usable
void demo_alloc_array(LinearAllocator& a) {
    constexpr size_t COUNT = 64;

    uint64_t t0 = time_us_64();
    uint32_t* nums = a.allocateArray<uint32_t>(COUNT);
    uint64_t dt = time_us_64() - t0;

    char msg[160];
    if (nums) {
        for (size_t i = 0; i < COUNT; i++) nums[i] = (uint32_t)(i * i);
        snprintf(msg, sizeof(msg), "allocArray<uint32_t>(%u) ok ptr=%p last=%lu",
                (unsigned)COUNT, (void*)nums, (unsigned long)nums[COUNT - 1]);
        report(a, dt, Outcome{msg, LogSeverity::INFO, PATTERN_OK});
    } else {
        snprintf(msg, sizeof(msg), "allocArray<uint32_t>(%u) FAILED", (unsigned)COUNT);
        report(a, dt, Outcome{msg, LogSeverity::ERROR, PATTERN_FAIL});
    }
}

void demo_alloc_object(LinearAllocator& a) {
    uint64_t t0 = time_us_64();
    Point* pt = a.allocateObject<Point>(Point{7, 42});
    uint64_t dt = time_us_64() - t0;

    char msg[160];
    if (pt) {
        snprintf(msg, sizeof(msg), "allocObject<Point>{7,42} ok ptr=%p x=%lu y=%lu",
                 (void*)pt, (unsigned long)pt->x, (unsigned long)pt->y);
        report(a, dt, Outcome{msg, LogSeverity::INFO, PATTERN_OK});
    } else {
        snprintf(msg, sizeof(msg), "allocObject<Point> FAILED");
        report(a, dt, Outcome{msg, LogSeverity::ERROR, PATTERN_FAIL});
    }
}

int main()
{
    LED_PIN = PICO_DEFAULT_LED_PIN;
    gpio_init(LED_PIN);
    gpio_set_dir(LED_PIN, GPIO_OUT);

    uart_log.init();

    LinearAllocator allocator;
    allocator.init(arena, ARENA_SIZE);
    char init_msg[64];
    snprintf(init_msg, sizeof(init_msg), "init cap=%u", (unsigned)ARENA_SIZE);
    uart_log.log(LogSeverity::INFO, init_msg);

    while (true) {
        // allocate and write a marker through the returned pointer to check if the pointer is still valid after reset
        uint8_t* ptr1 = static_cast<uint8_t*>(try_allocate(allocator, 512, "Alloc 512 bytes", "Alloc 512 bytes FAILED"));
        if (ptr1) *ptr1 = 0x42; // Write a test value to the allocated memory

        // typed allocation: multiple object, then a single object
        demo_alloc_array(allocator);  // 256 bytes - used 768
        demo_alloc_object(allocator); // 8 bytes - used 776

        auto mark = allocator.mark(); // save point at 776

        // Fill the arena, then prove OOM is reported, not crashed on
        try_allocate(allocator, 1024, "Alloc 1024 bytes", "Alloc 1024 bytes FAILED"); // used 1800
        try_allocate(allocator, 1024, "Alloc 1024 bytes", "Alloc 1024 bytes FAILED"); // used 2824
        // This allocation should fail ( 1272  free)
        try_allocate(allocator, 1536, "Alloc 1536 bytes", "Alloc 1536 bytes FAILED - exhausted"); 

        // Rewind to the previous mark, freeing the last two allocations, then show that we can allocate again
        try_rewind(allocator, mark, 
                Outcome{"Rewind to mark", LogSeverity::INFO, PATTERN_REWIND},
                Outcome{"Rewind to mark FAILED", LogSeverity::ERROR, PATTERN_FAIL});
        try_allocate(allocator, 1536, "Alloc 1536 bytes after rewind", "Alloc 1536 bytes FAILED");

        // Full reset, then prove old pointers no longer hold valid data
        auto stale = allocator.mark();
        do_reset(allocator); // Reset the allocator, invalidating all handles

        uint8_t after_rest_value = ptr1 ? *ptr1 : 0; // Check if ptr1 is still valid after reset
        char buf[110];
        snprintf(buf, sizeof(buf),
                "Pointer invalidation: wrote 0x42 before reset, read 0x%02X after reset", after_rest_value);
        uart_log.log(LogSeverity::WARNING, buf);
        sleep_ms(1000);
        
        // A mark taken before the reset is now stale, and rewinding to it should fail
        try_rewind(allocator, stale, 
                   Outcome{"Rewind to stale mark (not expected)", LogSeverity::ERROR, PATTERN_FAIL},
                   Outcome{"Rewind to stale mark rejected (expected)", LogSeverity::WARNING, PATTERN_REJECTED});
        
        sleep_ms(5000); // Wait before restarting the loop
    }
}
