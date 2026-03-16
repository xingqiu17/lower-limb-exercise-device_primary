#pragma once

#include <stdbool.h>
#include <stdint.h>

#include "driver/gpio.h"
#include "esp_intr_alloc.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "rom/ets_sys.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef bool boolean;
typedef uint8_t byte;

#define INPUT 0x0
#define OUTPUT 0x1
#define LOW 0x0
#define HIGH 0x1

#define PROGMEM
#define pgm_read_byte(addr) (*(const uint8_t *)(addr))

static inline void pinMode(uint8_t pin, uint8_t mode)
{
    gpio_set_direction((gpio_num_t)pin, mode == OUTPUT ? GPIO_MODE_OUTPUT : GPIO_MODE_INPUT);
}

static inline void digitalWrite(uint8_t pin, uint8_t value)
{
    gpio_set_level((gpio_num_t)pin, value ? 1 : 0);
}

static inline uint32_t micros(void)
{
    return (uint32_t)esp_timer_get_time();
}

static inline void delay(uint32_t ms)
{
    vTaskDelay(pdMS_TO_TICKS(ms));
}

static inline void delayMicroseconds(uint32_t us)
{
    ets_delay_us(us);
}

static inline void yield(void)
{
    taskYIELD();
}

static inline void noInterrupts(void)
{
    portDISABLE_INTERRUPTS();
}

static inline void interrupts(void)
{
    portENABLE_INTERRUPTS();
}

#ifdef __cplusplus
}
#endif
