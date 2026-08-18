#include <cstdio>
#include "pico/stdlib.h"
#include "pico/time.h"
#include "linear_allocator.hpp"
#include "uart_log.hpp"

static uint LED_PIN;
static UartLog uart_log;

void blink(int count, int on_ms, int off_ms)
{
    for (int i = 0; i < count; i++) {
        gpio_put(LED_PIN, 1);
        sleep_ms(on_ms);
        gpio_put(LED_PIN, 0);
        sleep_ms(off_ms);
    }
}

void log_event(LogSeverity severity, const LinearAllocator& allocator, uint64_t exec_us, const char* action) {
    char buf[220]; // Buffer to hold the log message
    snprintf(buf, sizeof(buf), "%s | used=%u free=%u high=%u cap=%u | %llu us",
            action, (unsigned int)allocator.used(), (unsigned int)allocator.freeSpace(),
            (unsigned int)allocator.highWater(), (unsigned int)allocator.capacity(),
            (unsigned long long)exec_us);
    uart_log.log(severity, buf);
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
        uint64_t t0, dt;

        t0 = time_us_64();
        uint8_t* ptr1 = static_cast<uint8_t*>(allocator.allocate(512));
        dt = time_us_64() - t0;
        log_event(ptr1 ? LogSeverity::INFO : LogSeverity::ERROR, allocator, dt, ptr1 ? "Alloc 512 bytes" : "Alloc 512 bytes FAILED");
        ptr1 ? blink(1, 500, 200) : blink(3, 100, 100);
        if (ptr1) *ptr1 = 0x42; // Write a test value to the allocated memory
        sleep_ms(1000);

        auto mark = allocator.mark();

        t0 = time_us_64();
        void* ptr2 = allocator.allocate(512);
        dt = time_us_64() - t0;
        log_event(ptr2 ? LogSeverity::INFO : LogSeverity::ERROR, allocator, dt, ptr2 ? "Alloc 512 bytes" : "Alloc 512 bytes FAILED");
        ptr2 ? blink(1, 500, 200) : blink(3, 100, 100);
        sleep_ms(1000);

        t0 = time_us_64();
        void* ptr3 = allocator.allocate(512);
        dt = time_us_64() - t0;
        log_event(ptr3 ? LogSeverity::INFO : LogSeverity::ERROR, allocator, dt, ptr3 ? "Alloc 512 bytes" : "Alloc 512 bytes FAILED");
        ptr3 ? blink(1, 500, 200) : blink(3, 100, 100);
        sleep_ms(1000);

        t0 = time_us_64();
        void* ptr4 = allocator.allocate(1024); // This allocation should fail
        dt = time_us_64() - t0;
        log_event(ptr4 ? LogSeverity::INFO : LogSeverity::ERROR, allocator, dt, ptr4 ? "Alloc 1024 bytes" : "Alloc 1024 bytes FAILED");
        ptr4 ? blink(1, 500, 200) : blink(3, 100, 100);
        sleep_ms(1000);

        t0 = time_us_64();
        bool rewind_success = allocator.rewind(mark); // Rewind to mark - freeing ptr2 and ptr3
        dt = time_us_64() - t0;
        log_event(rewind_success ? LogSeverity::INFO : LogSeverity::ERROR, allocator, dt, rewind_success ? "Rewind successful, freed 1024 bytes" : "Rewind FAILED");
        rewind_success ? blink(2, 300, 200) : blink(3, 100, 100);
        sleep_ms(1000);

        t0 = time_us_64();
        void* ptr5 = allocator.allocate(1024); // This allocation should now succeed
        dt = time_us_64() - t0;
        log_event(ptr5 ? LogSeverity::INFO : LogSeverity::ERROR, allocator, dt, ptr5 ? "Alloc 1024 bytes" : "Alloc 1024 bytes FAILED");
        ptr5 ? blink(1, 500, 200) : blink(3, 100, 100);
        sleep_ms(1000);

        auto stale = allocator.mark();

        t0 = time_us_64();
        allocator.reset(); // Reset the allocator, invalidating all handles
        dt = time_us_64() - t0;
        log_event(LogSeverity::INFO, allocator, dt, "Allocator reset");
        blink(1, 1000, 200);
        sleep_ms(1000);

        uint8_t after_rest_value = ptr1 ? *ptr1 : 0; // Check if ptr1 is still valid after reset
        char buf[100];
        snprintf(buf, sizeof(buf), "Pointer invalidation: wrote 0x42 before reset, read 0x%02X at %p after reset", after_rest_value, static_cast<void*>(ptr1));
        uart_log.log(LogSeverity::INFO, buf);
        sleep_ms(1000);
        
        bool rewind_stale = allocator.rewind(stale); // Attempt to rewind to a stale handle
        log_event(rewind_stale ? LogSeverity::ERROR : LogSeverity::WARNING, allocator, 0, rewind_stale ? "Rewind to stale mark succeeded (unexpected)" : "Rewind to stale mark failed (expected)");
        rewind_stale ? blink(3, 100, 100) : blink(4, 100, 100);
        
        sleep_ms(5000); // Wait before restarting the loop
    }
}
