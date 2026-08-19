#include <cstdio>
#include "pico/stdlib.h"
#include "pico/time.h"
#include "linear_allocator.hpp"
#include "uart_log.hpp"

static uint LED_PIN;
static UartLog uart_log;

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

void* try_allocate(LinearAllocator& allocator, size_t size, const char* ok_msg, const char* fail_msg) {
    uint64_t t0 = time_us_64();
    void* ptr = allocator.allocate(size);
    uint64_t dt = time_us_64() - t0;
   
    report(allocator, dt, ptr ? Outcome{ok_msg, LogSeverity::INFO, PATTERN_OK} 
                              : Outcome{fail_msg, LogSeverity::ERROR, PATTERN_FAIL});

    return ptr;
}

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

static uint8_t arena[2048];

int main()
{
    LED_PIN = PICO_DEFAULT_LED_PIN;
    gpio_init(LED_PIN);
    gpio_set_dir(LED_PIN, GPIO_OUT);

    uart_log.init();

    LinearAllocator allocator;
    allocator.init(arena, sizeof(arena));
    uart_log.log(LogSeverity::INFO, "LinearAllocator initialized with 2048 bytes.");

    while (true) {
        // allocate and write a marker through the returned pointer to check if the pointer is still valid after reset
        uint8_t* ptr1 = static_cast<uint8_t*>(try_allocate(allocator, 512, "Alloc 512 bytes", "Alloc 512 bytes FAILED"));
        if (ptr1) *ptr1 = 0x42; // Write a test value to the allocated memory

        auto mark = allocator.mark();

        // Fill the arena, then prove OOM is reported, not crashed on
        try_allocate(allocator, 512, "Alloc 512 bytes", "Alloc 512 bytes FAILED");
        try_allocate(allocator, 512, "Alloc 512 bytes", "Alloc 512 bytes FAILED");
        try_allocate(allocator, 1024, "Alloc 1024 bytes", "Alloc 1024 bytes FAILED - exhausted"); // This allocation should fail

        // Rewind to the previous mark, freeing the last two allocations, then show that we can allocate again
        try_rewind(allocator, mark, 
                Outcome{"Rewind to mark", LogSeverity::INFO, PATTERN_REWIND},
                Outcome{"Rewind to mark FAILED", LogSeverity::ERROR, PATTERN_FAIL});
        try_allocate(allocator, 1024, "Alloc 1024 bytes after rewind", "Alloc 1024 bytes FAILED");

        // Full reset, then prove old pointers no longer hold valid data
        auto stale = allocator.mark();
        do_reset(allocator); // Reset the allocator, invalidating all handles

        uint8_t after_rest_value = ptr1 ? *ptr1 : 0; // Check if ptr1 is still valid after reset
        char buf[100];
        snprintf(buf, sizeof(buf), "Pointer invalidation: wrote 0x42 before reset, read 0x%02X after reset", after_rest_value);
        uart_log.log(LogSeverity::WARNING, buf);
        sleep_ms(1000);
        
        // A mark taken before the reset is now stale, and rewinding to it should fail
        try_rewind(allocator, stale, 
                   Outcome{"Rewind to stale mark (not expected)", LogSeverity::ERROR, PATTERN_FAIL},
                   Outcome{"Rewind to stale mark rejected (expected)", LogSeverity::WARNING, PATTERN_REJECTED});
        
        sleep_ms(5000); // Wait before restarting the loop
    }
}
