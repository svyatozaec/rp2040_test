#pragma once
#include <cstdint>
#include <cstddef>
#include <cstring>
#include "hardware/uart.h"
#include "hardware/gpio.h"
#include "pico/time.h"

enum class LogLevel : uint8_t{
    DEBUG = 0,
    INFO = 1,
    WARNING = 2,
    ERROR = 3
};

class UartLog {
public:
    static constexpr uint UART_TX_PIN = 0; // GPIO pin for UART TX
    static constexpr uint UART_RX_PIN = 1; // GPIO pin for UART RX
    static constexpr uint UART_BAUD_RATE = 115200; // Baud rate for UART communication
    static constexpr uint8_t SYNC_BYTE = 0xAA; // Synchronization byte for log messages

    void init() {
        uart_init(uart0, UART_BAUD_RATE);
        gpio_set_function(UART_TX_PIN, GPIO_FUNC_UART);
        gpio_set_function(UART_RX_PIN, GPIO_FUNC_UART);
    }

    void log(LogLevel level, const char* message) {
        size_t message_length = strlen(message);
        if(message_length > 200) {
            message_length = 200; // Limit message length to 200 characters
        }

        uint8_t buffer[5 + 200]; // 1 byte for sync, 1 byte for level, 2 bytes for length, and up to 200 bytes for message
        uint32_t ts = to_ms_since_boot(get_absolute_time());
        buffer[0] = ts & 0xFF; // Timestamp LSB
        buffer[1] = (ts >> 8) & 0xFF; // Timestamp
        buffer[2] = (ts >> 16) & 0xFF; // Timestamp
        buffer[3] = (ts >> 24) & 0xFF; // Timestamp MSB
        buffer[4] = static_cast<uint8_t>(level); // Log level
        memcpy(buffer + 5, message, message_length); // Copy message into buffer

        size_t total_length = 5 + message_length;
        uint16_t crc = crc16_ccitt(buffer, total_length); // Calculate CRC16 for the message

        uint8_t frame[2 + sizeof(buffer) + 2]; // 2 bytes for sync and length, buffer, and 2 bytes for CRC
        frame[0] = SYNC_BYTE; // Sync byte
        frame[1] = static_cast<uint8_t>(total_length); // Length of the message
        memcpy(&frame[2], buffer, total_length); // Copy the message into the frame
        frame[2 + total_length] = crc & 0xFF; // CRC LSB
        frame[3 + total_length] = (crc >> 8) & 0xFF; // CRC MSB

        uart_write_blocking(uart0, frame, 2 + total_length + 2); // Send the frame over UART
    }

private:
    static uint16_t crc16_ccitt(const uint8_t* data, size_t length) {
        uint16_t crc = 0xFFFF; // Initial value
        for (size_t i = 0; i < length; ++i) {
            crc ^= static_cast<uint16_t>(data[i]) << 8; // XOR byte into the high byte of CRC
            for (int j = 0; j < 8; ++j) {
                if (crc & 0x8000) {
                    crc = (crc << 1) ^ 0x1021; // Polynomial used in CRC-CCITT
                } else {
                    crc <<= 1;
                }
            }
        }
        return crc;
    }
};    