#pragma once

#include <stddef.h>
#include <stdint.h>

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

typedef uint8_t byte;

static inline void delay(uint32_t ms)
{
    vTaskDelay(pdMS_TO_TICKS(ms));
}

class Stream {
public:
    virtual ~Stream() = default;
    virtual int available() = 0;
    virtual int read() = 0;
    virtual size_t write(uint8_t value) = 0;
    virtual size_t write(const uint8_t *buffer, size_t size)
    {
        if (buffer == nullptr) {
            return 0;
        }
        size_t written = 0;
        for (size_t i = 0; i < size; ++i) {
            written += write(buffer[i]);
        }
        return written;
    }
    virtual void flush() = 0;
};
