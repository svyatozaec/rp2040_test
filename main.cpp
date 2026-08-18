#include <stdio.h>
#include "pico/stdlib.h"
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

// each signal_* both blinks AND logs, so LED and UART always agree
void signal_ok(const char* message)       { blink(1, 500, 200); uart_log.log(LogLevel::INFO, message); } // 1 long blink
void signal_fail(const char* message)     { blink(3, 100, 100); uart_log.log(LogLevel::ERROR, message); } // 3 short blinks
void signal_rewind(const char* message)   { blink(2, 300, 200); uart_log.log(LogLevel::WARNING, message); } // 2 medium blinks
void signal_rejected(const char* message) { blink(4, 100, 100); uart_log.log(LogLevel::WARNING, message); } // 4 short blinks
void signal_reset(const char* message)    { blink(1, 1000, 200); uart_log.log(LogLevel::INFO, message); } // 1 very long blink

static uint8_t arena[2048];

int main()
{
    LED_PIN = PICO_DEFAULT_LED_PIN;
    gpio_init(LED_PIN);
    gpio_set_dir(LED_PIN, GPIO_OUT);

    uart_log.init();

    LinearAllocator allocator;
    allocator.init(arena, sizeof(arena));
    uart_log.log(LogLevel::INFO, "LinearAllocator initialized with 2048 bytes.");

    while (true) {
        void* ptr1 = allocator.allocate(512);
        ptr1 ? signal_ok("Alloc 512 bytes") : signal_fail("Alloc 512 bytes FAILED");
        sleep_ms(1000);

        auto handle1 = allocator.handle();

        void* ptr2 = allocator.allocate(512);
        ptr2 ? signal_ok("Alloc 512 bytes") : signal_fail("Alloc 512 bytes FAILED");
        sleep_ms(1000);

        void* ptr3 = allocator.allocate(512);
        ptr3 ? signal_ok("Alloc 512 bytes") : signal_fail("Alloc 512 bytes FAILED");
        sleep_ms(1000);

        void* ptr4 = allocator.allocate(1024); // This allocation should fail
        ptr4 ? signal_ok("Alloc 1024 bytes") : signal_fail("FAILED to allocate 1024 bytes - ");
        sleep_ms(1000);

        bool rewind_success = allocator.rewind(handle1); // Rewind to handle1 - freeing ptr2 and ptr3
        rewind_success ? signal_rewind("Rewind successful") : signal_fail("Rewind failed");
        sleep_ms(1000);

        void* ptr5 = allocator.allocate(1024); // This allocation should now succeed
        ptr5 ? signal_ok("Alloc 1024 bytes") : signal_fail("FAILED to allocate 1024 bytes");
        sleep_ms(1000);

        auto stale = allocator.handle();
        allocator.reset(); // Reset the allocator, invalidating all handles
        signal_reset("Allocator reset");
        sleep_ms(1000);

        bool rewind_stale = allocator.rewind(stale); // Attempt to rewind to a stale handle
        rewind_stale ? signal_rewind("BUG: stale rewind accepted") : signal_rejected("Stale rewind rejected as expected");
        sleep_ms(5000); // Wait before restarting the loop
    }
}
